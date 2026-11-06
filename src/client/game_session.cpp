#include "client/game_session.hpp"

#include <cmath>
#include <format>

#include <glm/gtc/matrix_transform.hpp>

#include "core/log.hpp"
#include "geom/primitives.hpp"
#include "world/rng.hpp"

namespace pb::client {
namespace {
glm::vec2 xz(glm::vec3 v) { return {v.x, v.z}; }
const glm::vec4 kWhite(1, 1, 1, 1), kDim(0.75f, 0.78f, 0.8f, 1), kWarn(1.0f, 0.55f, 0.35f, 1);
std::string short_name(const std::string& id) {
  if (id == "scrap_food") return "FOOD";
  if (id == "meat_bone") return "BONE";
  if (id == "water_bottle") return "WATER";
  if (id == "old_collar") return "COLLAR";
  return id.substr(0, 6);
}
}  // namespace

GameSession::GameSession(render::Renderer& r, const sim::GameplayConfig& cfg, const sim::DogCatalog& cat,
                         const world::WorldData& world, const sim::CollisionWorld& collision,
                         const std::filesystem::path& asset_root, const std::string& breed, glm::vec3 spawn,
                         const sim::NpcConfig& npc_cfg, const sim::WorldSimConfig& world_cfg)
    : cfg_(cfg), world_(world), collision_(collision), inv_(cfg.inventory_slots) {
  world_sim_ = std::make_unique<sim::WorldSim>(world_cfg, world, world.seed);
  {  // Mưa: 700 vệt mảnh trong khối 36x36x14 m quanh nhân vật (dịch chuyển theo thời gian)
    geom::MeshData rain;
    uint64_t s = 0xC0FFEE;
    const auto streak = geom::make_box({0.02f, 0.55f, 0.02f});
    for (int i = 0; i < 700; ++i) {
      s = world::splitmix64(s);
      const float x = static_cast<float>(s & 0xFFFF) / 65535.0f * 36 - 18;
      const float z = static_cast<float>((s >> 16) & 0xFFFF) / 65535.0f * 36 - 18;
      const float y = static_cast<float>((s >> 32) & 0xFFFF) / 65535.0f * 14;
      rain.append(streak, glm::translate(glm::mat4(1.0f), {x, y, z}));
    }
    rain_mesh_ = r.upload_mesh(rain, "fx/rain");
    beacon_ = r.upload_mesh(geom::make_cylinder(0.35f, 12.0f, 10), "fx/event_beacon");
  }
  library_ = std::make_unique<ModelLibrary>(r, asset_root);
  player_ = std::make_unique<DogActor>(r, *library_, cat, breed, spawn);
  nodes_ = sim::make_resource_nodes(world, cfg);
  nav_ = std::make_unique<sim::NavGraph>(world);
  npc_mgr_ = std::make_unique<sim::NpcManager>(world, collision, *nav_, cfg, npc_cfg, &nodes_);
  npc_view_ = std::make_unique<NpcView>(r, *library_, cat);
  for (const auto& c : world.chunks)
    for (const auto& p : c.pois)
      if (p.kind == world::PoiKind::Shelter) shelters_.emplace_back(world::to_m(p.x), world::to_m(p.z));
  // Hai chó NPC "tập cắn" đứng yên quanh điểm spawn (AI thật ở Phase 8).
  for (int i = 0; i < 2; ++i) {
    Npc n;
    glm::vec2 p = xz(spawn) + glm::vec2(i == 0 ? 3.5f : -3.0f, i == 0 ? 2.0f : 3.0f);
    collision.resolve(p, 0.4f, 0.0f, 0.3f);
    n.home = {p.x, world.height_at(p.x, p.y), p.y};
    n.actor = std::make_unique<DogActor>(r, *library_, cat, "shepherd", n.home);
    n.actor->set_yaw(i == 0 ? -2.0f : 2.4f);
    n.actor->set_tint(i == 0 ? glm::vec4(0.55f, 0.45f, 0.4f, 1) : glm::vec4(0.9f, 0.88f, 0.8f, 1));
    npcs_.push_back(std::move(n));
  }
  inv_.add(cfg, "water_bottle", 1);
  marker_ = r.upload_mesh(geom::make_cylinder(0.12f, 6.0f, 8), "session/sniff_marker");
  say("WASD move, SHIFT sprint, SPACE jump, E interact, J bite, K dodge, F sniff, Z rest");
  log::info("game", "session: {} nguồn tài nguyên, {} nơi trú ẩn, {} NPC tập", nodes_.size(), shelters_.size(), npcs_.size());
}

void GameSession::say(std::string msg) {
  message_ = std::move(msg);
  message_t_ = 4.0f;
}

void GameSession::interact(const std::string& key) {
  const glm::vec2 pos = xz(player_->state().pos);
  if (key == "Z") {
    resting_ = !resting_;
    say(resting_ ? "Resting... (move to stop)" : "Stopped resting");
    return;
  }
  if (key == "G") {
    if (auto* n = sim::nearest_node(nodes_, pos, cfg_.interact_range_m, world::PoiKind::Food)) {
      const auto r = sim::forage_at(*n, pos, inv_, cfg_);
      say(r == sim::InteractResult::Ok ? "Took food into bag" : std::string("Cannot take: ") + to_string(r));
    }
    return;
  }
  if (key.size() == 1 && key[0] >= '1' && key[0] <= '6') {
    const int slot = key[0] - '1';
    const auto r = sim::use_item(inv_, slot, sv_, cfg_);
    say(r == sim::InteractResult::Ok ? std::format("Used slot {}", slot + 1) : std::format("Slot {}: cannot use", slot + 1));
    return;
  }
  // E: hành động theo ngữ cảnh — sự kiện trước
  for (const auto& ev : world_sim_->events()) {
    if (glm::distance(ev.pos, pos) > 3.0f) continue;
    if (ev.type == sim::EventType::LostPuppy || ev.type == sim::EventType::RescueSignal) {
      world_sim_->complete_event(ev.id);
      inv_.add(cfg_, "meat_bone", 1);
      say(ev.type == sim::EventType::LostPuppy ? "You found the lost puppy! (+bone)" : "You answered the rescue call! (+bone)");
      return;
    }
  }
  if (auto* n = sim::nearest_node(nodes_, pos, cfg_.interact_range_m)) {
    const auto r = sim::consume_at(*n, pos, sv_, cfg_);
    if (r == sim::InteractResult::Ok) say(n->kind == world::PoiKind::Water ? "Drank water" : "Ate food");
    else say(std::string(n->kind == world::PoiKind::Water ? "Water: " : "Food: ") +
             (r == sim::InteractResult::Full ? "not needed now" : r == sim::InteractResult::Depleted ? "empty, wait" : "failed"));
    return;
  }
  for (const auto& s : shelters_)
    if (glm::distance(s, pos) < 4.0f) {
      resting_ = true;
      say("Resting in shelter (x2 recovery)");
      return;
    }
  say("Nothing to interact with here");
}

void GameSession::fixed_update(const InputState& in_raw, const std::vector<std::string>& pressed,
                               const geom::Camera25D& cam, float dt) {
  time_ += dt;
  InputState in = in_raw;
  const auto& sc = cfg_.survival;
  const bool alive = sv_.life == sim::LifeState::Alive;
  const bool moving_input = glm::length(in.move) > 0.1f;
  if (moving_input) resting_ = false;

  // Hành động rời rạc
  if (alive) {
    if (in.interact) interact("E");
    for (const auto& k : pressed)
      if (k == "G" || k == "Z" || (k.size() == 1 && k[0] >= '1' && k[0] <= '6')) interact(k);
    if (in.dodge) {
      sim::Combatant me{player_->state().pos, player_->state().yaw, true, &sv_, &cb_};
      say(sim::request_dodge(me, cfg_) ? "Dodge!" : "Cannot dodge (cooldown/stamina)");
    }
  }
  sniffing_ = in.sniff && alive;

  // Nhảy tốn thể lực; không đủ thì không nhảy
  if (in.jump && (!player_->state().grounded || !sim::spend_stamina(sv_, sc, sc.jump_stamina))) in.jump = false;
  sprinting_ = in.sprint && moving_input && sim::can_sprint(sv_, sc, sprinting_);
  const auto wfx = world_sim_->effects();
  player_->fixed_update(in, cam, collision_, dt, sim::speed_multiplier(sv_, sc) * wfx.move, sprinting_, !alive || resting_);

  const glm::vec3 pp = player_->state().pos;
  sim::Activity act;
  act.moving = glm::length(player_->state().vel) > 0.3f;
  act.sprinting = sprinting_ && act.moving;
  act.resting = resting_;
  for (const auto& s : shelters_) act.in_shelter |= glm::distance(s, xz(pp)) < 4.0f;
  sim::tick_survival(sv_, sc, act, dt);
  sim::tick_combat(cb_, cfg_.combat, dt);
  {  // Thế giới động: giờ, thời tiết, hồi tài nguyên theo lịch, sự kiện
    const glm::vec3 me = player_->state().pos;
    const auto ch = world_sim_->tick(dt, std::span(&me, 1), nodes_);
    for (const auto& e : ch.started) {
      const glm::vec2 d = e.pos - xz(me);
      say(std::format("Event: {} ({:.0f} m away)", sim::to_string(e.type), glm::length(d)));
    }
    for (const auto& e : ch.ended)
      if (!e.completed) say(std::format("Event ended: {}", sim::to_string(e.type)));
    if (ch.weather_changed) say(std::format("Weather: {}", sim::to_string(world_sim_->weather())));
    npc_mgr_->set_human_density(world_sim_->pedestrian_density());
    // Chớp khi có bão
    lightning_ = std::max(0.0f, lightning_ - dt * 6.0f);
    if (wfx.storm > 0.5f && (lightning_timer_ -= dt) <= 0) {
      fx_rng_ = world::splitmix64(fx_rng_);
      lightning_ = 1.0f;
      lightning_timer_ = 4.0f + static_cast<float>(fx_rng_ % 8000) / 1000.0f;
    }
  }

  // Chiến đấu với NPC tập
  target_ = -1;
  float best = 3.0f;
  for (size_t i = 0; i < npcs_.size(); ++i) {
    const float d = glm::distance(xz(npcs_[i].actor->state().pos), xz(pp));
    if (d < best && npcs_[i].sv.life == sim::LifeState::Alive) {
      best = d;
      target_ = static_cast<int>(i);
    }
  }
  // NPC AI gần nhất trong tầm cắn (ưu tiên hơn NPC tập nếu gần hơn)
  ai_target_ = 0;
  for (const auto& n : npc_mgr_->npcs()) {
    const float d = glm::distance(xz(n.motor.pos), xz(pp));
    if (d < best && n.sv.life == sim::LifeState::Alive) {
      best = d;
      ai_target_ = n.id;
      target_ = -1;
    }
  }
  if (in.attack && alive && ai_target_ != 0) {
    sim::Npc* npc = npc_mgr_->npcs_mut().empty() ? nullptr : npc_mgr_->find(ai_target_);
    if (npc) {
      sim::Combatant me{pp, player_->state().yaw, true, &sv_, &cb_};
      sim::Combatant them{npc->motor.pos, npc->motor.yaw, false, &npc->sv, &npc->cb};
      const auto zone = world_.zone_at(static_cast<world::cm>(pp.x * 100), static_cast<world::cm>(pp.z * 100));
      const auto o = sim::request_bite(me, them, zone, cfg_, time_);
      if (o.result == sim::BiteResult::Hit) {
        npc_mgr_->on_attacked(npc->id, 1);
        noises_.push_back({xz(pp), 15.0f, 1});
      }
      say(o.result == sim::BiteResult::Hit
              ? std::format("Bit the {}! -{:.0f} HP{}", npc->kind == sim::NpcKind::Human ? "person" : "dog", o.damage,
                            o.target_downed ? " - down!" : "")
              : std::string("Bite: ") + to_string(o.result));
    }
  } else if (in.attack && alive) {
    if (target_ < 0) {
      say("No target in range");
    } else {
      auto& npc = npcs_[static_cast<size_t>(target_)];
      sim::Combatant me{pp, player_->state().yaw, true, &sv_, &cb_};
      sim::Combatant them{npc.actor->state().pos, npc.actor->state().yaw, false, &npc.sv, &npc.cb};
      const auto zone = world_.zone_at(static_cast<world::cm>(pp.x * 100), static_cast<world::cm>(pp.z * 100));
      const auto o = sim::request_bite(me, them, zone, cfg_, time_);
      say(o.result == sim::BiteResult::Hit ? std::format("Bite hit! -{:.0f} HP{}{}", o.damage, o.target_staggered ? " (stagger)" : "",
                                                          o.target_downed ? " - down!" : "")
                                           : std::string("Bite: ") + to_string(o.result));
    }
  }
  for (auto& n : npcs_) {
    sim::tick_survival(n.sv, sc, {}, dt);
    sim::tick_combat(n.cb, cfg_.combat, dt);
    InputState none;
    n.actor->fixed_update(none, cam, collision_, dt, 1.0f, false, true);
    if (n.sv.life != sim::LifeState::Alive && (n.respawn_timer += dt) > 15.0f) {
      n.sv = {};
      n.respawn_timer = 0;
      n.actor->teleport(n.home);
    }
  }

  // ---- NPC AI (cùng luật server sẽ chạy)
  {
    sim::PlayerView pv{1, player_->state().pos, player_->state().yaw, &sv_, &cb_};
    const auto events = npc_mgr_->update(dt, std::span(&pv, 1), noises_);
    noises_.clear();
    npc_view_->after_tick(npc_mgr_->npcs());
    for (const auto& e : events) {
      switch (e.type) {
        case sim::AiEvent::Type::FeedPlayer:
          say(inv_.add(cfg_, "meat_bone", 1) == 0 ? "A kind person gave you a meaty bone!" : "Someone offered food, bag full");
          break;
        case sim::AiEvent::Type::BitePlayer: say(std::format("A stray dog bit you! -{:.0f} HP", e.value)); break;
        case sim::AiEvent::Type::KickPlayer: say(std::format("Kicked by a person! -{:.0f} HP", e.value)); break;
        case sim::AiEvent::Type::Shout: say("Someone shouts at you to go away"); break;
        case sim::AiEvent::Type::CallHelp: say("Someone is calling for help!"); break;
        case sim::AiEvent::Type::Growl: say("A dog growls at you..."); break;
      }
    }
  }

  // Gục -> hết giờ -> hồi sinh
  if (sv_.life == sim::LifeState::Dead) {
    const auto o = sim::respawn(sv_, inv_, xz(pp), world_, cfg_);
    player_->teleport({o.pos.x, world_.height_at(o.pos.x, o.pos.y), o.pos.y});
    say(std::format("You collapsed. Recovered at shelter (lost {} items)", o.dropped_items));
  }

  // Gợi ý tương tác
  prompt_.clear();
  if (alive) {
    if (auto* n = sim::nearest_node(nodes_, xz(pp), cfg_.interact_range_m))
      prompt_ = n->kind == world::PoiKind::Water ? std::format("[E] Drink ({} left)", n->remaining)
                                                  : std::format("[E] Eat  [G] Take ({} left)", n->remaining);
    else
      for (const auto& s : shelters_)
        if (glm::distance(s, xz(pp)) < 4.0f) prompt_ = "[E] Rest in shelter";
    if (prompt_.empty() && target_ >= 0) prompt_ = "[J] Bite  [K] Dodge";
  }
  message_t_ = std::max(0.0f, message_t_ - dt);
}

void GameSession::update_visual(float dt) {
  player_->update_visual(dt);
  npc_view_->update_visual(npc_mgr_->npcs(), dt);
  for (auto& n : npcs_) n.actor->update_visual(dt);
}

void GameSession::append_draws(float alpha, bool debug, std::vector<render::DrawItem>& out) {
  player_->append_draws(alpha, debug, out);
  npc_view_->append(npc_mgr_->npcs(), alpha, debug, out);
  for (auto& n : npcs_)
    if (n.sv.life == sim::LifeState::Alive || std::fmod(n.respawn_timer, 0.5f) < 0.25f) n.actor->append_draws(alpha, debug, out);
  // Cột đánh dấu sự kiện (màu theo loại)
  for (const auto& ev : world_sim_->events()) {
    static const glm::vec4 colors[] = {{1.0f, 0.85f, 0.3f, 1}, {1.0f, 0.55f, 0.1f, 1}, {0.9f, 0.15f, 0.1f, 1},
                                       {0.3f, 0.9f, 1.0f, 1}, {0.8f, 0.8f, 0.85f, 1}};
    render::Material m{colors[static_cast<size_t>(ev.type)]};
    m.unlit = true;
    m.cutaway = false;
    out.push_back({beacon_, glm::translate(glm::mat4(1.0f), glm::vec3(ev.pos.x, world_.height_at(ev.pos.x, ev.pos.y), ev.pos.y)), m, false});
  }
  // Mưa quanh nhân vật
  const auto wfx = world_sim_->effects();
  if (wfx.rain > 0.05f) {
    const glm::vec3 p = player_->position(alpha);
    rain_phase_ = std::fmod(world_sim_->sim_time() * 18.0f, 14.0f);
    for (int layer = 0; layer < (wfx.rain > 0.7f ? 2 : 1); ++layer) {
      const float y = p.y - static_cast<float>(rain_phase_) + static_cast<float>(layer) * 7.0f;
      render::Material m{{0.7f, 0.75f, 0.85f, 1}, 0.0f, 0.2f};
      m.unlit = true;
      m.cutaway = false;
      out.push_back({rain_mesh_, glm::translate(glm::mat4(1.0f), glm::vec3(p.x, y, p.z)), m, false});
      out.push_back({rain_mesh_, glm::translate(glm::mat4(1.0f), glm::vec3(p.x + 7, y + 14, p.z - 5)), m, false});
    }
  }
  if (sniffing_) {  // đánh hơi: cột mùi trên nguồn tài nguyên trong bán kính (mưa làm giảm)
    const glm::vec2 pp = xz(player_->state().pos);
    for (const auto& n : nodes_) {
      if (glm::distance(n.pos, pp) > cfg_.sniff_radius_m * wfx.scent) continue;
      render::Material m{n.kind == world::PoiKind::Water ? glm::vec4(0.3f, 0.7f, 1.0f, 1) : glm::vec4(1.0f, 0.6f, 0.2f, 1)};
      m.unlit = true;
      m.cutaway = false;
      if (n.remaining == 0) m.base_color *= 0.35f;
      out.push_back({marker_, glm::translate(glm::mat4(1.0f), glm::vec3(n.pos.x, world_.height_at(n.pos.x, n.pos.y), n.pos.y)), m, false});
    }
  }
}

void GameSession::draw_hud(render::Overlay& o, int w, int h, float hour) const {
  const float s = std::max(0.75f, static_cast<float>(h) / 720.0f);
  const float x = 16 * s, bw = 180 * s, bh = 12 * s;
  float y = 16 * s;
  struct Bar {
    const char* label;
    float v;
    glm::vec4 c;
  };
  const auto hud = sim::make_hud(sv_, inv_, cfg_, world_.zone_at(static_cast<world::cm>(player_->state().pos.x * 100),
                                                                 static_cast<world::cm>(player_->state().pos.z * 100)));
  const Bar bars[] = {{"HP", hud.hp, {0.85f, 0.2f, 0.2f, 0.95f}},
                      {"FOOD", hud.hunger, {0.95f, 0.6f, 0.15f, 0.95f}},
                      {"WATER", hud.thirst, {0.2f, 0.55f, 0.95f, 0.95f}},
                      {"STAMINA", hud.stamina, {0.95f, 0.85f, 0.2f, 0.95f}},
                      {"REST", hud.rest, {0.6f, 0.4f, 0.9f, 0.95f}}};
  o.rect(x - 8 * s, y - 8 * s, bw + 140 * s, 5 * (bh + 8 * s) + 12 * s, {0, 0, 0, 0.6f});
  for (const auto& b : bars) {
    o.text(x, y, b.label, kWhite, 1.6f * s);
    o.bar(x + 80 * s, y, bw, bh, b.v, b.c, {0.15f, 0.15f, 0.15f, 0.8f});
    o.text(x + 86 * s + bw, y, std::format("{:.0f}", b.v * 100), kDim, 1.6f * s);
    y += bh + 8 * s;
  }
  y += 6 * s;
  std::string flags;
  if (hud.low_hunger) flags += "HUNGRY ";
  if (hud.low_thirst) flags += "THIRSTY ";
  if (hud.tired) flags += "TIRED ";
  if (hud.resting) flags += "RESTING ";
  if (hud.downed) flags += std::format("DOWN ({:.0f}s) ", hud.downed_left);
  if (!flags.empty()) o.text(x, y, flags, kWarn, 1.8f * s);

  // Góc phải: vùng + giờ
  std::string zone = hud.zone == "safe" ? "SAFE ZONE" : hud.zone == "contested" ? "CONTESTED (PvP)" : "RESTRICTED";
  const std::string tr = std::format("{} | {:02d}:{:02d}", zone, static_cast<int>(hour), static_cast<int>(std::fmod(hour, 1.0f) * 60));
  const float tw = render::Overlay::text_width(tr, 1.8f * s);
  o.rect(static_cast<float>(w) - tw - 28 * s, 10 * s, tw + 18 * s, 26 * s, {0, 0, 0, 0.35f});
  o.text(static_cast<float>(w) - tw - 19 * s, 16 * s, tr, hud.zone == "contested" ? kWarn : kWhite, 1.8f * s);

  // Túi đồ (dưới giữa)
  const float slot = 58 * s, gap = 6 * s;
  const float total = static_cast<float>(hud.slots.size()) * (slot + gap) - gap;
  const float sx = (static_cast<float>(w) - total) * 0.5f, sy = static_cast<float>(h) - slot - 16 * s;
  for (size_t i = 0; i < hud.slots.size(); ++i) {
    const float bx = sx + static_cast<float>(i) * (slot + gap);
    o.rect(bx, sy, slot, slot, {0, 0, 0, 0.45f});
    o.text(bx + 4 * s, sy + 4 * s, std::to_string(i + 1), kDim, 1.4f * s);
    if (!hud.slots[i].empty()) {
      o.text(bx + 4 * s, sy + 22 * s, short_name(hud.slots[i].item), kWhite, 1.4f * s);
      o.text(bx + 4 * s, sy + 40 * s, std::format("x{}", hud.slots[i].count), kDim, 1.4f * s);
    }
  }
  float py = sy - 34 * s;
  if (!prompt_.empty()) {
    const float pw = render::Overlay::text_width(prompt_, 2.0f * s);
    o.rect((static_cast<float>(w) - pw) * 0.5f - 10 * s, py - 6 * s, pw + 20 * s, 26 * s, {0, 0, 0, 0.5f});
    o.text((static_cast<float>(w) - pw) * 0.5f, py, prompt_, kWhite, 2.0f * s);
    py -= 32 * s;
  }
  if (message_t_ > 0) {
    const float mw = render::Overlay::text_width(message_, 1.8f * s);
    o.text((static_cast<float>(w) - mw) * 0.5f, py, message_, {1, 1, 0.8f, std::min(1.0f, message_t_)}, 1.8f * s);
  }
  if (target_ >= 0 || ai_target_ != 0) {
    const sim::SurvivalState* tsv = nullptr;
    std::string name = "TRAINING DOG";
    if (target_ >= 0) {
      tsv = &npcs_[static_cast<size_t>(target_)].sv;
    } else {
      for (const auto& n : npc_mgr_->npcs())
        if (n.id == ai_target_) {
          tsv = &n.sv;
          name = std::string(n.kind == sim::NpcKind::Human ? "PERSON (" : "DOG (") + to_string(n.pers) + ")";
        }
    }
    if (tsv) {
      const float tx = (static_cast<float>(w) - 200 * s) * 0.5f;
      o.text(tx, 16 * s, name, kWhite, 1.6f * s);
      o.bar(tx, 34 * s, 200 * s, 8 * s, tsv->hp / cfg_.survival.max_hp, {0.85f, 0.25f, 0.2f, 0.95f}, {0.15f, 0.15f, 0.15f, 0.8f});
    }
  }
  {
    const auto& st = npc_mgr_->stats();
    const std::string ai = std::format("NPC {} | AI {:.2f} ms", st.active, st.last_ms);
    o.text(static_cast<float>(w) - render::Overlay::text_width(ai, 1.4f * s) - 16 * s, 44 * s, ai, kDim, 1.4f * s);
  }
  if (sniffing_) o.text(x, y + 26 * s, "SNIFFING: blue = water, orange = food", {0.6f, 0.9f, 1.0f, 1}, 1.6f * s);
  // Thời tiết + danh sách sự kiện (góc phải)
  {
    float ey = 64 * s;
    const std::string wtxt = std::format("Day {} | {}", world_sim_->clock().day() + 1, sim::to_string(world_sim_->weather()));
    o.rect(static_cast<float>(w) - 260 * s, 40 * s, 250 * s, (40 + 18 * static_cast<float>(world_sim_->events().size())) * s,
           {0, 0, 0, 0.45f});
    o.text(static_cast<float>(w) - render::Overlay::text_width(wtxt, 1.6f * s) - 16 * s, ey, wtxt, kWhite, 1.6f * s);
    const glm::vec2 pp = xz(player_->state().pos);
    for (const auto& ev : world_sim_->events()) {
      ey += 18 * s;
      const glm::vec2 d = ev.pos - pp;
      const std::string t = std::format("! {} {:.0f}m ({:.0f}s)", sim::to_string(ev.type), glm::length(d),
                                        ev.ends - world_sim_->sim_time());
      o.text(static_cast<float>(w) - render::Overlay::text_width(t, 1.5f * s) - 16 * s, ey, t, {1, 0.85f, 0.4f, 1}, 1.5f * s);
    }
  }
}

void GameSession::debug_next_weather() {
  const auto next = static_cast<sim::Weather>((static_cast<int>(world_sim_->weather()) + 1) % static_cast<int>(sim::Weather::Count));
  world_sim_->force_weather(next, false);
  say(std::format("[debug] weather -> {}", sim::to_string(next)));
}

void GameSession::debug_event(std::optional<sim::EventType> t) {
  const auto type = t.value_or(static_cast<sim::EventType>(world::splitmix64(fx_rng_++) % static_cast<int>(sim::EventType::Count)));
  const auto e = world_sim_->force_event(type, xz(player_->state().pos), nodes_);
  say(e ? std::format("[debug] event {} at {:.0f} m", sim::to_string(type), glm::distance(e->pos, xz(player_->state().pos)))
        : std::string("[debug] no valid location for event"));
}

bool GameSession::qa_teleport_near(world::PoiKind kind) {
  const auto* n = sim::nearest_node(nodes_, xz(player_->state().pos), 1e9f, kind);
  if (!n) return false;
  glm::vec2 p = n->pos + glm::vec2(0.8f, 0.0f);
  collision_.resolve(p, 0.32f, world_.height_at(p.x, p.y), 0.3f);
  if (glm::distance(p, n->pos) > cfg_.interact_range_m) p = n->pos;  // không đặt được cạnh -> đứng ngay tại nguồn
  player_->teleport({p.x, world_.height_at(p.x, p.y), p.y});
  return true;
}

bool GameSession::qa_face_npc(size_t i, float distance) {
  if (i >= npcs_.size()) return false;
  const glm::vec3 t = npcs_[i].actor->state().pos;
  const glm::vec2 p = xz(t) + glm::vec2(0.0f, -distance);
  player_->teleport({p.x, world_.height_at(p.x, p.y), p.y});
  player_->set_yaw(0.0f);  // nhìn +Z, về phía NPC
  return true;
}

void GameSession::qa_set_needs(float hunger, float thirst, float rest) {
  sv_.hunger = hunger;
  sv_.thirst = thirst;
  sv_.rest = rest;
}

std::string GameSession::summary() const {
  return std::format("t={:.0f}s hp={:.0f} hunger={:.1f} thirst={:.1f} stamina={:.0f} rest={:.1f} life={} items={}", time_,
                     sv_.hp, sv_.hunger, sv_.thirst, sv_.stamina, sv_.rest, static_cast<int>(sv_.life),
                     inv_.count_of("scrap_food") + inv_.count_of("water_bottle") + inv_.count_of("meat_bone"));
}

}  // namespace pb::client
