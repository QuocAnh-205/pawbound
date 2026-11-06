#include "sim/npc.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include "world/rng.hpp"

namespace pb::sim {

const char* to_string(NpcKind k) { return k == NpcKind::Human ? "human" : "dog"; }
const char* to_string(Personality p) {
  static const char* n[] = {"neutral", "friendly", "fearful", "hostile", "vendor", "aggressive"};
  return n[static_cast<size_t>(p)];
}
const char* to_string(NpcState s) {
  static const char* n[] = {"idle", "walk", "wait", "flee", "approach", "feed", "scare", "call_help", "defend",
                            "roam", "sniff", "seek_food", "eat", "seek_water", "drink", "rest", "follow", "growl", "fight"};
  return n[static_cast<size_t>(s)];
}

NpcConfig load_npc_config(const std::filesystem::path& path, ConfigDiagnostics& d) {
  NpcConfig c;
  auto j = load_json_config(path, d, true);
  ConfigReader r(j, "", d);
  const char* dn[] = {"downtown", "residential", "park", "industrial"};
  auto h = r.child("humans_per_chunk");
  auto g = r.child("dogs_per_chunk");
  for (int i = 0; i < world::kDistrictCount; ++i) {
    h.read(dn[i], c.humans_per_chunk[static_cast<size_t>(i)], 0, 50);
    g.read(dn[i], c.dogs_per_chunk[static_cast<size_t>(i)], 0, 50);
  }
  r.read("active_radius_chunks", c.active_radius_chunks, 0, 8);
  r.read("max_active_npcs", c.max_active, 0, 5000);
  auto lod = r.child("ai_lod");
  lod.read("near_m", c.lod_near_m, 1, 500);
  lod.read("far_tick_interval", c.far_tick_interval, 1, 60);
  r.read("max_path_requests_per_tick", c.max_path_requests_per_tick, 1, 1000);
  auto pw = r.child("personality_weights");
  pw.read("friendly", c.w_friendly, 0, 1000);
  pw.read("fearful", c.w_fearful, 0, 1000);
  pw.read("hostile", c.w_hostile, 0, 1000);
  pw.read("neutral", c.w_neutral, 0, 1000);
  auto hu = r.child("human");
  hu.read("walk", c.h_walk, 0.1, 5);
  hu.read("run", c.h_run, 0.5, 10);
  hu.read("sight_m", c.h_sight, 1, 100);
  hu.read("fov_deg", c.h_fov, 10, 360);
  hu.read("hearing_m", c.h_hearing, 0, 200);
  hu.read("radius", c.h_radius, 0.1, 1);
  hu.read("kick_damage", c.kick_damage, 0, 100);
  hu.read("kick_cooldown_s", c.kick_cd, 0.1, 60);
  hu.read("feed_cooldown_s", c.feed_cd, 1, 10000);
  hu.read("notoriety_wary", c.notoriety_wary, 0, 1000);
  auto dg = r.child("dog");
  dg.read("sight_m", c.d_sight, 1, 100);
  dg.read("fov_deg", c.d_fov, 10, 360);
  dg.read("hearing_m", c.d_hearing, 0, 200);
  dg.read("aggressive_percent", c.d_aggressive_pct, 0, 100);
  dg.read("friendly_percent", c.d_friendly_pct, 0, 100);
  dg.read("growl_s", c.growl_s, 0, 10);
  dg.read("fight_range_m", c.fight_range, 1, 30);
  dg.read("follow_s", c.follow_s, 1, 600);
  dg.read("flee_hp_fraction", c.flee_hp, 0, 1);
  dg.read("need_rate_mult", c.need_rate_mult, 0.1, 100);
  auto pa = r.child("path");
  int me = static_cast<int>(c.max_expanded);
  pa.read("max_expanded", me, 10, 1000000);
  c.max_expanded = static_cast<uint32_t>(me);
  pa.read("stuck_s", c.stuck_s, 0.5, 60);
  pa.read("goal_timeout_s", c.goal_timeout_s, 1, 3600);
  pa.read("roam_radius_m", c.roam_radius, 5, 500);
  return c;
}

namespace {
glm::vec2 xz(glm::vec3 v) { return {v.x, v.z}; }
uint32_t next(uint64_t& s) {
  s = world::splitmix64(s);
  return static_cast<uint32_t>(s >> 32);
}
float frand(uint64_t& s) { return static_cast<float>(next(s)) / 4294967296.0f; }
}  // namespace

NpcManager::NpcManager(const world::WorldData& w, const CollisionWorld& col, NavGraph& nav, const GameplayConfig& gc,
                       const NpcConfig& nc, std::vector<ResourceNode>* resources)
    : world_(w), col_(col), nav_(nav), gc_(gc), nc_(nc), resources_(resources) {}

Npc* NpcManager::find(uint32_t id) {
  for (auto& n : npcs_)
    if (n.id == id) return &n;
  return nullptr;
}

void NpcManager::change(Npc& n, NpcState s) {
  if (n.state == s) return;
  n.state = s;
  n.state_t = 0;
}

// ---------------------------------------------------------------- spawn/despawn theo chunk
void NpcManager::sync_chunks(std::span<const PlayerView> players) {
  std::unordered_set<int> want;
  for (const auto& p : players) {
    const int cx = static_cast<int>(p.pos.x / world::to_m(world_.chunk_size));
    const int cz = static_cast<int>(p.pos.z / world::to_m(world_.chunk_size));
    for (int z = cz - nc_.active_radius_chunks; z <= cz + nc_.active_radius_chunks; ++z)
      for (int x = cx - nc_.active_radius_chunks; x <= cx + nc_.active_radius_chunks; ++x)
        if (x >= 0 && z >= 0 && x < world_.chunks_x && z < world_.chunks_z) want.insert(world_.chunk_index(x, z));
  }
  // Gỡ NPC của chunk không còn quan tâm
  std::erase_if(npcs_, [&](const Npc& n) { return !want.contains(n.home_chunk); });
  for (auto it = active_.begin(); it != active_.end();) it = want.contains(*it) ? std::next(it) : active_.erase(it);
  std::vector<int> add;
  for (int c : want)
    if (!active_.contains(c)) add.push_back(c);
  std::sort(add.begin(), add.end());  // thứ tự cố định -> deterministic
  for (int c : add) {
    active_.insert(c);
    spawn_chunk(c);
  }
}

std::vector<Npc> NpcManager::make_chunk_npcs(int chunk) const {
  std::vector<Npc> out;
  const int cx = chunk % world_.chunks_x, cz = chunk / world_.chunks_x;
  const float cs = world::to_m(world_.chunk_size);
  // Nút nav trong chunk = nơi có thể xuất hiện (vỉa hè, lối đi)
  std::vector<uint32_t> spots;
  for (uint32_t i = 0; i < nav_.size(); ++i) {
    const glm::vec2 p = nav_.pos(i);
    if (p.x >= static_cast<float>(cx) * cs && p.x < static_cast<float>(cx + 1) * cs && p.y >= static_cast<float>(cz) * cs &&
        p.y < static_cast<float>(cz + 1) * cs)
      spots.push_back(i);
  }
  if (spots.empty()) return out;
  uint64_t rng = world::sub_seed(world_.seed, "npc", static_cast<uint64_t>(chunk));
  const auto d = static_cast<size_t>(world_.district_at(static_cast<world::cm>((static_cast<float>(cx) + 0.5f) * cs * 100),
                                                        static_cast<world::cm>((static_cast<float>(cz) + 0.5f) * cs * 100)));
  auto count = [&](float mean) {  // phần nguyên + xác suất phần lẻ
    const int base = static_cast<int>(mean);
    return base + (frand(rng) < mean - static_cast<float>(base) ? 1 : 0);
  };
  const int humans = count(nc_.humans_per_chunk[d]);
  const int dogs = count(nc_.dogs_per_chunk[d]);
  for (int i = 0; i < humans + dogs; ++i) {
    Npc n;
    n.id = (static_cast<uint32_t>(chunk) << 12) | static_cast<uint32_t>(i);
    n.home_chunk = chunk;
    // Mỗi NPC có hạt giống riêng theo (seed, chunk, chỉ số) -> thêm/bớt một NPC không đổi NPC khác
    n.rng = world::sub_seed(world_.seed, "npc_entity", static_cast<uint64_t>(chunk), static_cast<uint64_t>(i));
    const glm::vec2 p = nav_.pos(spots[next(n.rng) % spots.size()]) + glm::vec2(frand(n.rng) - 0.5f, frand(n.rng) - 0.5f);
    n.motor.pos = {p.x, world_.height_at(p.x, p.y), p.y};
    n.motor.yaw = frand(n.rng) * 6.28318f;
    n.presence = frand(n.rng);
    if (i < humans) {
      n.kind = NpcKind::Human;
      const int tot = nc_.w_friendly + nc_.w_fearful + nc_.w_hostile + nc_.w_neutral;
      int r = static_cast<int>(next(n.rng) % static_cast<uint32_t>(std::max(tot, 1)));
      n.pers = (r -= nc_.w_friendly) < 0 ? Personality::Friendly
               : (r -= nc_.w_fearful) < 0 ? Personality::Fearful
               : (r -= nc_.w_hostile) < 0 ? Personality::Hostile
                                          : Personality::Neutral;
      n.stats.walk_speed = nc_.h_walk;
      n.stats.trot_speed = (nc_.h_walk + nc_.h_run) * 0.5f;
      n.stats.run_speed = nc_.h_run;
      n.stats.sprint_speed = nc_.h_run * 1.2f;
      n.stats.radius = nc_.h_radius;
      n.stats.jump_speed = 0;
      n.state = NpcState::Walk;
    } else {
      n.kind = NpcKind::Dog;
      n.presence = 0;  // chó hoang có mặt mọi lúc
      const int r = static_cast<int>(next(n.rng) % 100);
      n.pers = r < nc_.d_aggressive_pct ? Personality::Aggressive
               : r < nc_.d_aggressive_pct + nc_.d_friendly_pct ? Personality::Friendly
                                                               : Personality::Neutral;
      n.stats.radius = 0.3f;
      n.state = NpcState::Roam;
      n.sv.hunger = 50 + frand(n.rng) * 50;
      n.sv.thirst = 50 + frand(n.rng) * 50;
    }
    n.last_hp = n.sv.hp;
    out.push_back(std::move(n));
  }
  return out;
}

void NpcManager::spawn_chunk(int chunk) {
  for (auto& n : make_chunk_npcs(chunk)) {
    if (static_cast<int>(npcs_.size()) >= nc_.max_active) return;
    if (n.kind == NpcKind::Human && n.presence >= density_) continue;
    npcs_.push_back(std::move(n));
  }
}

void NpcManager::set_human_density(float d) { density_ = std::max(0.0f, d); }

void NpcManager::refresh_density() {
  if (std::abs(density_ - applied_density_) < 0.05f) return;
  applied_density_ = density_;
  std::vector<int> chunks(active_.begin(), active_.end());
  std::sort(chunks.begin(), chunks.end());
  for (int c : chunks)
    for (auto& n : make_chunk_npcs(c)) {
      if (n.kind != NpcKind::Human) continue;
      const bool want = n.presence < density_;
      Npc* have = find(n.id);
      if (want && !have && static_cast<int>(npcs_.size()) < nc_.max_active) npcs_.push_back(std::move(n));
      else if (!want && have && have->state != NpcState::Defend && have->state != NpcState::Flee)
        std::erase_if(npcs_, [&](const Npc& x) { return x.id == n.id; });
    }
}

// ---------------------------------------------------------------- di chuyển
bool NpcManager::set_goal(Npc& n, glm::vec2 goal) {
  const auto a = nav_.nearest(xz(n.motor.pos));
  const auto b = nav_.nearest(goal);
  n.has_goal = false;
  n.path.clear();
  n.path_i = 0;
  if (!a || !b) return false;
  if (path_budget_ <= 0) {
    ++stats_.path_deferred;  // hết ngân sách tick này -> thử lại tick sau
    return false;
  }
  --path_budget_;
  ++stats_.path_requests;
  const auto& r = nav_.find_path_cached(*a, *b);
  if (r.status != PathResult::Status::Found) {
    ++stats_.path_failed;
    return false;  // fallback: gọi lại sẽ chọn đích khác, không lặp vô hạn
  }
  n.path = r.nodes;
  n.goal = goal;
  n.has_goal = true;
  n.goal_t = 0;
  n.stuck_t = 0;
  n.stuck_ref = xz(n.motor.pos);
  return true;
}

bool NpcManager::random_goal(Npc& n, float radius) {
  const glm::vec2 here = xz(n.motor.pos);
  for (int attempt = 0; attempt < 4; ++attempt) {
    const float a = frand(n.rng) * 6.28318f, d = radius * (0.3f + 0.7f * frand(n.rng));
    const glm::vec2 g = here + glm::vec2(std::cos(a), std::sin(a)) * d;
    // Ở trong chunk đang hoạt động để NPC không đi lạc ra vùng sẽ bị despawn
    const int cx = static_cast<int>(g.x / world::to_m(world_.chunk_size)), cz = static_cast<int>(g.y / world::to_m(world_.chunk_size));
    if (cx < 0 || cz < 0 || cx >= world_.chunks_x || cz >= world_.chunks_z || !active_.contains(world_.chunk_index(cx, cz))) continue;
    if (set_goal(n, g)) return true;
  }
  return false;
}

void NpcManager::move_toward(Npc& n, float dt, glm::vec2 target, float speed, bool sprint, float stop_dist) {
  MotorInput in;
  const glm::vec2 d = target - xz(n.motor.pos);
  const float len = glm::length(d);
  if (len > stop_dist) {
    // Độ lớn quyết định dáng đi (motor: <0,5 đi bộ, <0,85 chạy nước kiệu, còn lại chạy) — không bật cờ walk
    // (cờ walk sẽ nhân tốc độ đi bộ với độ lớn lần nữa).
    in.move = d / len * speed;
    in.sprint = sprint;
  }
  step_motor(n.motor, in, n.stats, col_, dt);
}

bool NpcManager::follow_path(Npc& n, float dt, float speed, bool sprint) {
  if (!n.has_goal || n.path_i >= n.path.size()) {
    step_motor(n.motor, {}, n.stats, col_, dt);
    return true;
  }
  n.goal_t += dt;
  const glm::vec2 wp = n.path_i + 1 < n.path.size() ? nav_.pos(n.path[n.path_i]) : n.goal;
  if (glm::distance(xz(n.motor.pos), wp) < 1.2f) ++n.path_i;
  move_toward(n, dt, wp, speed, sprint, 0.3f);
  // Kẹt: không tiến được -> bỏ đích (sẽ chọn đích khác), không lặp mãi
  n.stuck_t += dt;
  if (n.stuck_t > nc_.stuck_s) {
    if (glm::distance(xz(n.motor.pos), n.stuck_ref) < 0.5f || n.goal_t > nc_.goal_timeout_s) {
      n.has_goal = false;
      return true;
    }
    n.stuck_t = 0;
    n.stuck_ref = xz(n.motor.pos);
  }
  return n.path_i >= n.path.size();
}

// ---------------------------------------------------------------- AI
void NpcManager::on_attacked(uint32_t npc_id, uint32_t player_id) {
  Npc* n = find(npc_id);
  if (!n) return;
  n->target_player = player_id;
  n->alert = true;
  if (n->kind == NpcKind::Human) change(*n, n->pers == Personality::Hostile ? NpcState::Defend : NpcState::Flee);
  else change(*n, n->pers == Personality::Aggressive ? NpcState::Fight : NpcState::Flee);
}

void NpcManager::tick_human(Npc& n, float dt, const PlayerView* p, float dist, bool sees,
                            std::span<const PlayerView> players, std::vector<AiEvent>& ev) {
  const float notoriety = p && p->cb ? p->cb->notoriety : 0.0f;
  const bool fighting = p && p->cb && p->cb->since_last_combat < 3.0f;
  const bool wary = notoriety >= nc_.notoriety_wary || fighting || n.alert;
  n.feed_cd = std::max(0.0f, n.feed_cd - dt);
  n.kick_cd = std::max(0.0f, n.kick_cd - dt);

  // Phản ứng theo tính cách khi thấy người chơi
  if (p && sees && n.state != NpcState::Flee && n.state != NpcState::Defend && n.state != NpcState::Feed) {
    switch (n.pers) {
      case Personality::Friendly:
        if (!wary && n.feed_cd <= 0 && dist < 10 && n.state != NpcState::Approach) {
          n.target_player = p->id;
          change(n, NpcState::Approach);
        } else if (wary && dist < 6) {
          change(n, NpcState::Flee);
        }
        break;
      case Personality::Fearful:
        if (dist < (wary ? 9.0f : 5.0f)) change(n, NpcState::Flee);
        break;
      case Personality::Hostile:
        if (dist < 7 && n.state != NpcState::Scare && n.state != NpcState::CallHelp) {
          n.target_player = p->id;
          change(n, wary && notoriety >= nc_.notoriety_wary * 2 ? NpcState::CallHelp : NpcState::Scare);
        }
        break;
      default: break;  // trung lập: bỏ qua
    }
  }

  switch (n.state) {
    case NpcState::Walk:
      if (!n.has_goal && !random_goal(n, nc_.roam_radius)) {
        change(n, NpcState::Wait);
        break;
      }
      if (follow_path(n, dt, 0.4f, false)) change(n, NpcState::Wait);
      break;
    case NpcState::Wait:
    case NpcState::Idle:
      step_motor(n.motor, {}, n.stats, col_, dt);
      if (n.state_t > 3.0f) {  // dừng chân 3 s rồi đi tiếp
        n.has_goal = false;
        change(n, NpcState::Walk);
      }
      break;
    case NpcState::Flee: {
      if (!p || dist > 16 || n.state_t > 8) {
        n.alert = false;
        n.has_goal = false;
        change(n, NpcState::Walk);
        break;
      }
      const glm::vec2 away = glm::normalize(xz(n.motor.pos) - xz(p->pos) + glm::vec2(1e-3f));
      move_toward(n, dt, xz(n.motor.pos) + away * 10.0f, 1.0f, false, 0.1f);
      break;
    }
    case NpcState::Approach:
      if (!p || wary || n.state_t > 12) {
        change(n, NpcState::Walk);
        break;
      }
      move_toward(n, dt, xz(p->pos), 0.4f, false, 1.4f);
      if (dist < 1.8f) change(n, NpcState::Feed);
      break;
    case NpcState::Feed:
      step_motor(n.motor, {}, n.stats, col_, dt);
      if (n.state_t >= 1.0f) {
        if (p) ev.push_back({AiEvent::Type::FeedPlayer, n.id, p->id, 1});
        n.feed_cd = nc_.feed_cd;
        change(n, NpcState::Walk);
      }
      break;
    case NpcState::Scare:
      if (!p || dist > 12 || n.state_t > 6) {
        change(n, NpcState::Walk);
        break;
      }
      move_toward(n, dt, xz(p->pos), 0.4f, false, 2.2f);
      if (n.state_t < dt * 1.5f) ev.push_back({AiEvent::Type::Shout, n.id, p->id, 0});
      break;
    case NpcState::CallHelp:
      step_motor(n.motor, {}, n.stats, col_, dt);
      if (n.state_t < dt * 1.5f) {
        ev.push_back({AiEvent::Type::CallHelp, n.id, p ? p->id : 0, 0});
        for (auto& o : npcs_)  // người xung quanh cảnh giác
          if (o.kind == NpcKind::Human && o.id != n.id && glm::distance(xz(o.motor.pos), xz(n.motor.pos)) < nc_.h_hearing)
            o.alert = true;
      }
      if (n.state_t > 3) change(n, NpcState::Walk);
      break;
    case NpcState::Defend: {
      const PlayerView* t = nullptr;
      for (const auto& pl : players)
        if (pl.id == n.target_player) t = &pl;
      if (!t || n.state_t > 6 || glm::distance(xz(t->pos), xz(n.motor.pos)) > 8) {
        change(n, NpcState::Walk);
        break;
      }
      move_toward(n, dt, xz(t->pos), 0.8f, false, 1.0f);
      if (glm::distance(xz(t->pos), xz(n.motor.pos)) < 1.4f && n.kick_cd <= 0 && t->sv &&
          t->sv->life == LifeState::Alive) {
        apply_damage(*t->sv, nc_.kick_damage);
        n.kick_cd = nc_.kick_cd;
        ev.push_back({AiEvent::Type::KickPlayer, n.id, t->id, nc_.kick_damage});
      }
      break;
    }
    default: change(n, NpcState::Walk); break;
  }
}

void NpcManager::tick_dog(Npc& n, float dt, const PlayerView* p, float dist, bool sees, std::vector<AiEvent>& ev) {
  // Nhu cầu (nhanh hơn người chơi để hành vi tìm ăn/uống thấy được trong vài phút)
  Activity act;
  act.moving = glm::length(n.motor.vel) > 0.3f;
  act.resting = n.state == NpcState::Rest;
  SurvivalConfig sc = gc_.survival;
  sc.hunger_full_to_empty_s /= nc_.need_rate_mult;
  sc.thirst_full_to_empty_s /= nc_.need_rate_mult;
  sc.rest_full_to_empty_s /= nc_.need_rate_mult;
  tick_survival(n.sv, sc, act, dt);
  tick_combat(n.cb, gc_.combat, dt);
  if (n.sv.life != LifeState::Alive) {
    step_motor(n.motor, {}, n.stats, col_, dt);
    return;
  }
  const bool hurt = n.sv.hp < gc_.survival.max_hp * nc_.flee_hp;
  if (hurt && n.state == NpcState::Fight) change(n, NpcState::Flee);

  // Phản ứng với người chơi
  const float notoriety = p && p->cb ? p->cb->notoriety : 0.0f;
  if (p && sees && p->sv && p->sv->life == LifeState::Alive && !hurt) {
    if (n.pers == Personality::Aggressive && dist < 6 && n.state != NpcState::Growl && n.state != NpcState::Fight) {
      n.target_player = p->id;
      change(n, NpcState::Growl);
      ev.push_back({AiEvent::Type::Growl, n.id, p->id, 0});
    } else if (n.pers == Personality::Friendly && notoriety < nc_.notoriety_wary && dist < 8 && n.state == NpcState::Roam) {
      n.target_player = p->id;
      change(n, NpcState::Follow);
    }
  }

  switch (n.state) {
    case NpcState::Roam:
      if (n.sv.thirst < 40 && resources_) change(n, NpcState::SeekWater);
      else if (n.sv.hunger < 40 && resources_) change(n, NpcState::SeekFood);
      else if (n.sv.rest < 30) change(n, NpcState::Rest);
      else if (!n.has_goal && !random_goal(n, nc_.roam_radius * 0.6f)) change(n, NpcState::Sniff);
      else if (follow_path(n, dt, 0.6f, false)) change(n, frand(n.rng) < 0.5f ? NpcState::Sniff : NpcState::Roam);
      break;
    case NpcState::Sniff:
      step_motor(n.motor, {}, n.stats, col_, dt);
      if (n.state_t > 2.5f) {
        n.has_goal = false;
        change(n, NpcState::Roam);
      }
      break;
    case NpcState::SeekFood:
    case NpcState::SeekWater: {
      const auto kind = n.state == NpcState::SeekFood ? world::PoiKind::Food : world::PoiKind::Water;
      ResourceNode* best = nullptr;
      float bd = 80;
      for (auto& r : *resources_)
        if (r.kind == kind && r.remaining > 0 && glm::distance(r.pos, xz(n.motor.pos)) < bd) {
          bd = glm::distance(r.pos, xz(n.motor.pos));
          best = &r;
        }
      if (!best || n.state_t > 40) {  // không tìm được: quay lại lang thang (không treo trạng thái)
        change(n, NpcState::Roam);
        break;
      }
      if (bd < gc_.interact_range_m * 0.9f) {
        consume_at(*best, xz(n.motor.pos), n.sv, gc_);
        change(n, kind == world::PoiKind::Food ? NpcState::Eat : NpcState::Drink);
        break;
      }
      if (!n.has_goal || glm::distance(n.goal, best->pos) > 1.0f) set_goal(n, best->pos);
      if (n.has_goal) follow_path(n, dt, 0.7f, false);
      else move_toward(n, dt, best->pos, 0.6f, false, 0.5f);
      break;
    }
    case NpcState::Eat:
    case NpcState::Drink:
    case NpcState::Rest:
      step_motor(n.motor, {}, n.stats, col_, dt);
      if (n.state_t > (n.state == NpcState::Rest ? 20.0f : 3.0f)) {
        n.has_goal = false;
        change(n, NpcState::Roam);
      }
      break;
    case NpcState::Follow:
      if (!p || dist > 20 || n.state_t > nc_.follow_s || notoriety >= nc_.notoriety_wary) {
        change(n, NpcState::Roam);
        break;
      }
      move_toward(n, dt, xz(p->pos), dist > 6 ? 1.0f : 0.5f, false, 2.0f);
      break;
    case NpcState::Growl:
      step_motor(n.motor, {}, n.stats, col_, dt);
      if (p) n.motor.yaw = std::atan2(p->pos.x - n.motor.pos.x, p->pos.z - n.motor.pos.z);
      if (n.state_t > nc_.growl_s) change(n, p && dist < nc_.fight_range ? NpcState::Fight : NpcState::Roam);
      break;
    case NpcState::Fight: {
      if (!p || dist > nc_.fight_range * 3 || n.state_t > 20 || !p->sv || p->sv->life != LifeState::Alive) {
        change(n, NpcState::Roam);
        break;
      }
      move_toward(n, dt, xz(p->pos), 1.0f, false, 1.0f);
      if (dist < gc_.combat.bite_range_m * 0.95f) {
        n.motor.yaw = std::atan2(p->pos.x - n.motor.pos.x, p->pos.z - n.motor.pos.z);
        Combatant me{n.motor.pos, n.motor.yaw, false, &n.sv, &n.cb};
        Combatant them{p->pos, p->yaw, true, p->sv, p->cb};
        const auto o = request_bite(me, them, world::ZoneRule::Contested, gc_, time_);
        if (o.result == BiteResult::Hit) ev.push_back({AiEvent::Type::BitePlayer, n.id, p->id, o.damage});
      }
      break;
    }
    case NpcState::Flee: {
      if (!p || dist > 18 || n.state_t > 8) {
        change(n, NpcState::Roam);
        break;
      }
      const glm::vec2 away = glm::normalize(xz(n.motor.pos) - xz(p->pos) + glm::vec2(1e-3f));
      move_toward(n, dt, xz(n.motor.pos) + away * 10.0f, 1.0f, !hurt, 0.1f);
      break;
    }
    default: change(n, NpcState::Roam); break;
  }
}

void NpcManager::tick_npc(Npc& n, float dt, std::span<const PlayerView> players, std::span<const Noise> noises,
                          std::vector<AiEvent>& ev) {
  n.state_t += dt;
  // Người chơi gần nhất + nhận biết
  const PlayerView* p = nullptr;
  float dist = 1e9f;
  for (const auto& pl : players) {
    const float d = glm::distance(xz(pl.pos), xz(n.motor.pos));
    if (d < dist) {
      dist = d;
      p = &pl;
    }
  }
  bool sees = false;
  if (p) {
    const bool human = n.kind == NpcKind::Human;
    const float range = human ? nc_.h_sight : nc_.d_sight;
    const float fov = human ? nc_.h_fov : nc_.d_fov;
    if (dist < 1.5f) {
      sees = true;  // quá gần: luôn nhận biết
    } else if (dist < range) {
      const glm::vec2 fwd(std::sin(n.motor.yaw), std::cos(n.motor.yaw));
      const glm::vec2 to = glm::normalize(xz(p->pos) - xz(n.motor.pos));
      sees = glm::dot(fwd, to) >= std::cos(glm::radians(fov * 0.5f)) &&
             !col_.segment_blocked(xz(n.motor.pos), xz(p->pos), n.motor.pos.y + 1.2f);
    }
    // Nghe: tiếng động trong bán kính -> quay về phía nguồn và coi như thấy (nếu cùng người chơi gần nhất)
    for (const auto& nz : noises)
      if (glm::distance(nz.pos, xz(n.motor.pos)) < std::min(nz.radius, human ? nc_.h_hearing : nc_.d_hearing)) {
        n.alert = n.alert || human;
        if (nz.source_player == p->id && dist < range * 1.5f) sees = true;
      }
  }
  // Bị trúng đòn (HP giảm) mà không qua on_attacked -> vẫn phản ứng
  if (n.sv.hp < n.last_hp - 0.01f && p && n.state != NpcState::Flee && n.state != NpcState::Defend && n.state != NpcState::Fight)
    on_attacked(n.id, p->id);
  n.last_hp = n.sv.hp;
  if (n.kind == NpcKind::Human) tick_human(n, dt, p, dist, sees, players, ev);
  else tick_dog(n, dt, p, dist, sees, ev);
}

std::vector<AiEvent> NpcManager::update(float dt, std::span<const PlayerView> players, std::span<const Noise> noises) {
  const auto t0 = std::chrono::steady_clock::now();
  ++tick_;
  time_ += dt;
  sync_chunks(players);
  if (tick_ % 30 == 0) refresh_density();  // mỗi giây kiểm tra mật độ theo giờ/thời tiết
  path_budget_ = nc_.max_path_requests_per_tick;
  std::vector<AiEvent> ev;
  for (auto& n : npcs_) {
    float near = 1e9f;
    for (const auto& p : players) near = std::min(near, glm::distance(xz(p.pos), xz(n.motor.pos)));
    if (near > nc_.lod_near_m) {
      // AI LOD: NPC xa tick thưa hơn, gộp dt (logic vẫn đúng theo thời gian)
      n.lod_accum += dt;
      if ((tick_ + n.id) % static_cast<uint64_t>(nc_.far_tick_interval) != 0) continue;
      const float acc = n.lod_accum;
      n.lod_accum = 0;
      ++stats_.lod_ticks;
      tick_npc(n, acc, players, noises, ev);
    } else {
      const float acc = n.lod_accum + dt;
      n.lod_accum = 0;
      ++stats_.full_ticks;
      tick_npc(n, acc, players, noises, ev);
    }
  }
  stats_.active = npcs_.size();
  stats_.last_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  stats_.max_ms = std::max(stats_.max_ms, stats_.last_ms);
  return ev;
}

std::string NpcManager::debug_line(const Npc& n) const {
  return std::format("{:#x} {} {} {} hp {:.0f} đói {:.0f} khát {:.0f} path {}/{}", n.id, to_string(n.kind), to_string(n.pers),
                     to_string(n.state), n.sv.hp, n.sv.hunger, n.sv.thirst, n.path_i, n.path.size());
}

}  // namespace pb::sim
