#include "sim/advanced.hpp"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

#include <glm/geometric.hpp>

namespace pb::sim {

// ============================================================================ cấu hình
AdvancedConfig parse_advanced_config(std::string_view text, ConfigDiagnostics& diag) {
  AdvancedConfig c;
  const auto j = parse_json_config(text, diag);
  ConfigReader root(j, "", diag);
  {
    auto r = root.child("capture");
    auto& k = c.capture;
    r.read("enabled", k.enabled);
    if (r.has("active_hours") && r.raw()["active_hours"].is_array()) {
      k.active_hours.clear();
      for (const auto& h : r.raw()["active_hours"])
        if (h.is_number_integer() && h.get<int>() >= 0 && h.get<int>() < 24) k.active_hours.push_back(h.get<int>());
        else diag.errors.push_back("capture.active_hours: giờ phải là số nguyên 0..23");
    }
    r.read("spawn_chance_per_min", k.spawn_chance_per_min, 0, 10);
    r.read("spawn_min_dist_m", k.spawn_min_dist_m, 5, 200);
    r.read("spawn_max_dist_m", k.spawn_max_dist_m, 5, 200);
    r.read("max_thieves", k.max_thieves, 0, 64);
    r.read("min_session_s", k.min_session_s, 0, 86400);
    r.read("warn_distance_m", k.warn_distance_m, 2, 100);
    r.read("min_warning_s", k.min_warning_s, 0.5, 60);
    r.read("give_up_s", k.give_up_s, 1, 600);
    r.read("escape_distance_m", k.escape_distance_m, 2, 200);
    r.read("grab_distance_m", k.grab_distance_m, 0.5, 10);
    r.read("thief_speed", k.thief_speed, 0.5, 20);
    r.read("thief_hp", k.thief_hp, 1, 1000);
    r.read("hold_max_s", k.hold_max_s, 10, 3600);  // trần cứng 1 giờ: không bao giờ khóa người chơi lâu
    r.read("hold_radius_m", k.hold_radius_m, 1, 50);
    r.read("rescue_channel_s", k.rescue_channel_s, 0.5, 120);
    r.read("rescue_range_m", k.rescue_range_m, 0.5, 20);
    r.read("immunity_s", k.immunity_s, 0, 86400);
    r.read("rescue_reward_bones", k.rescue_reward_bones, 0, 500);
    if (k.spawn_min_dist_m > k.spawn_max_dist_m) diag.errors.push_back("capture: spawn_min_dist_m > spawn_max_dist_m");
    if (k.spawn_min_dist_m <= k.warn_distance_m) diag.errors.push_back("capture: kẻ trộm phải xuất hiện NGOÀI tầm cảnh báo (spawn_min_dist_m > warn_distance_m)");
  }
  {
    auto r = root.child("companion");
    auto& k = c.companion;
    r.read("enabled", k.enabled);
    r.read("recruit_item", k.recruit_item);
    r.read("follow_distance_m", k.follow_distance_m, 0.5, 10);
    r.read("speed", k.speed, 0.5, 20);
    r.read("danger_alert_m", k.danger_alert_m, 1, 200);
    r.read("food_alert_m", k.food_alert_m, 1, 200);
    r.read("alert_cooldown_s", k.alert_cooldown_s, 1, 600);
  }
  {
    auto r = root.child("skills");
    auto& k = c.skills;
    r.read("max_level", k.max_level, 1, 10);
    r.read("xp_per_level", k.xp_per_level, 1, 1e6);
    r.read("speed_bonus_per_level", k.speed_bonus_per_level, 0, 0.02);  // trần cứng: tối đa +2%/cấp
    r.read("scent_bonus_per_level", k.scent_bonus_per_level, 0, 0.2);
    r.read("strength_bonus_per_level", k.strength_bonus_per_level, 0, 0.1);
  }
  if (j.contains("quests") && j["quests"].is_array()) {
    static const std::pair<const char*, QuestObjective> objs[] = {{"forage", QuestObjective::Forage}, {"drink", QuestObjective::Drink},
        {"travel", QuestObjective::Travel}, {"rescue", QuestObjective::Rescue}, {"escape_thief", QuestObjective::EscapeThief}};
    for (const auto& q : j["quests"]) {
      QuestDef d;
      ConfigReader r(q, "quests[]", diag);
      r.read("id", d.id);
      r.read("title", d.title);
      std::string obj;
      r.read("objective", obj);
      const auto it = std::find_if(std::begin(objs), std::end(objs), [&](const auto& p) { return obj == p.first; });
      if (it == std::end(objs)) diag.errors.push_back("quest " + d.id + ": objective lạ '" + obj + "'");
      else d.objective = it->second;
      r.read("count", d.count, 0.01, 1e6);
      r.read("reward_bones", d.reward_bones, 0, 500);
      r.read("timeout_s", d.timeout_s, 10, 7 * 86400);
      r.read("daily", d.daily);
      if (d.id.empty() || d.id.size() > 32) diag.errors.push_back("quest: id rỗng/quá dài");
      c.quests.push_back(std::move(d));
    }
  }
  return c;
}

AdvancedConfig load_advanced_config(const std::filesystem::path& path, ConfigDiagnostics& diag) {
  const auto j = load_json_config(path, diag, true);
  return parse_advanced_config(j.dump(), diag);
}

const char* to_string(CaptureState s) {
  switch (s) {
    case CaptureState::Normal: return "normal";
    case CaptureState::Threatened: return "threatened";
    case CaptureState::Captured: return "captured";
    case CaptureState::RescueInProgress: return "rescue_in_progress";
  }
  return "?";
}
const char* to_string(CaptureEvent::Type t) {
  switch (t) {
    case CaptureEvent::Type::Warning: return "warning";
    case CaptureEvent::Type::Escaped: return "escaped";
    case CaptureEvent::Type::Captured: return "captured";
    case CaptureEvent::Type::RescueStarted: return "rescue_started";
    case CaptureEvent::Type::RescueInterrupted: return "rescue_interrupted";
    case CaptureEvent::Type::Rescued: return "rescued";
    case CaptureEvent::Type::Released: return "released";
    case CaptureEvent::Type::ThiefDefeated: return "thief_defeated";
  }
  return "?";
}

// ============================================================================ bắt chó
CaptureSystem::CaptureSystem(const CaptureConfig& cfg, glm::vec2 hold_point, glm::vec2 release_point, uint64_t seed)
    : cfg_(cfg), hold_(hold_point), release_(release_point), rng_(seed | 1) {}

float CaptureSystem::frand() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 7;
  rng_ ^= rng_ << 17;
  return static_cast<float>(rng_ >> 40) / static_cast<float>(1ull << 24);
}

bool CaptureSystem::hit_thief(uint32_t thief_id, float damage) {
  for (auto& t : thieves_)
    if (t.id == thief_id && !t.fleeing) {
      t.hp -= damage;
      return true;
    }
  return false;
}

void CaptureSystem::on_player_removed(uint32_t player) {
  for (auto& t : thieves_)
    if (t.target == player) t.fleeing = true;
}

void CaptureSystem::restore(CapturePlayer& p, int64_t unix_now) {
  auto& s = *p.status;
  if (s.state == CaptureState::Threatened) s.state = CaptureState::Normal;  // kẻ trộm cũ không còn
  if (s.state == CaptureState::RescueInProgress) s.state = CaptureState::Captured;
  s.rescuer = 0;
  s.rescue_progress = 0;
  if (s.state == CaptureState::Captured) {
    if (s.hold_until > unix_now) {
      if (p.move_pos) *p.move_pos = hold_;  // relog KHÔNG thoát được
    } else {
      s.state = CaptureState::Normal;  // hết hạn trong lúc offline -> được thả ở chỗ an toàn
      s.immune_until = std::max(s.immune_until, unix_now + static_cast<int64_t>(cfg_.immunity_s));
      if (p.move_pos) *p.move_pos = release_;
    }
  }
  capture_seq_ = std::max(capture_seq_, s.capture_id);
}

std::vector<CaptureEvent> CaptureSystem::tick(std::vector<CapturePlayer>& players, double now_s, int64_t unix_now, float world_hour,
                                              float dt, const std::function<bool(glm::vec2)>& zone_ok) {
  std::vector<CaptureEvent> ev;
  auto find = [&](uint32_t id) -> CapturePlayer* {
    for (auto& p : players)
      if (p.id == id) return &p;
    return nullptr;
  };
  const bool night = std::find(cfg_.active_hours.begin(), cfg_.active_hours.end(), static_cast<int>(world_hour) % 24) != cfg_.active_hours.end();

  // ---- 1. spawn (mỗi giây tung xúc xắc một lần theo xác suất/phút)
  if (cfg_.enabled && night && now_s >= next_spawn_roll_) {
    next_spawn_roll_ = now_s + 1.0;
    for (auto& p : players) {
      if (static_cast<int>(thieves_.size()) >= cfg_.max_thieves) break;
      auto& s = *p.status;
      const bool eligible = p.online && p.alive && s.state == CaptureState::Normal && s.immune_until <= unix_now && !p.in_safe_zone &&
                            p.session_s >= cfg_.min_session_s && zone_ok(p.pos) &&
                            std::none_of(thieves_.begin(), thieves_.end(), [&](const Thief& t) { return t.target == p.id; });
      if (!eligible || frand() >= cfg_.spawn_chance_per_min / 60.0f) continue;
      const float a = frand() * 6.2831853f;
      const float d = cfg_.spawn_min_dist_m + frand() * (cfg_.spawn_max_dist_m - cfg_.spawn_min_dist_m);
      thieves_.push_back({++thief_seq_, p.id, p.pos + glm::vec2(std::cos(a), std::sin(a)) * d, cfg_.thief_hp, now_s, false});
    }
  }

  // ---- 2. kẻ trộm di chuyển + chuyển trạng thái nạn nhân
  for (auto& t : thieves_) {
    CapturePlayer* p = find(t.target);
    if (!p || !p->online) t.fleeing = true;
    if (t.hp <= 0 && !t.fleeing) {
      t.fleeing = true;
      if (p) ev.push_back({CaptureEvent::Type::ThiefDefeated, p->id, t.id, 0});
    }
    if (t.fleeing) {
      if (p && p->status->state == CaptureState::Threatened && p->status->thief == t.id) {
        p->status->state = CaptureState::Normal;
        p->status->immune_until = unix_now + static_cast<int64_t>(cfg_.immunity_s);
        ev.push_back({CaptureEvent::Type::Escaped, p->id, t.id, 0});
      }
      t.pos += glm::vec2(1, 0) * cfg_.thief_speed * dt;  // chạy khỏi hiện trường rồi biến mất
      continue;
    }
    auto& s = *p->status;
    const float dist = glm::distance(t.pos, p->pos);
    if (s.state == CaptureState::Normal && dist <= cfg_.warn_distance_m) {
      s.state = CaptureState::Threatened;
      s.thief = t.id;
      s.threatened_at = now_s;
      ev.push_back({CaptureEvent::Type::Warning, p->id, t.id, 0});
    }
    if (s.state == CaptureState::Threatened && s.thief == t.id) {
      // Thoát: chạy đủ xa, vào khu an toàn, hoặc kẻ trộm bỏ cuộc
      if (dist >= cfg_.escape_distance_m || p->in_safe_zone || now_s - s.threatened_at >= cfg_.give_up_s) {
        t.fleeing = true;
        s.state = CaptureState::Normal;
        s.immune_until = unix_now + static_cast<int64_t>(cfg_.immunity_s);
        ev.push_back({CaptureEvent::Type::Escaped, p->id, t.id, 0});
        continue;
      }
      // Bắt: chỉ khi đã cảnh báo đủ lâu (luôn có cơ hội phản ứng)
      if (dist <= cfg_.grab_distance_m && now_s - s.threatened_at >= cfg_.min_warning_s && p->alive) {
        s.state = CaptureState::Captured;
        s.capture_id = s.capture_id + 1;  // tăng theo nạn nhân (được lưu)
        s.hold_until = unix_now + static_cast<int64_t>(cfg_.hold_max_s);
        s.rescuer = 0;
        s.rescue_progress = 0;
        if (p->move_pos) *p->move_pos = hold_;  // không mất nhân vật, không mất đồ: chỉ bị đưa tới khu giữ
        t.fleeing = true;
        ev.push_back({CaptureEvent::Type::Captured, p->id, t.id, s.capture_id});
        continue;
      }
    }
    // Tiến về nạn nhân
    if (dist > 0.01f) t.pos += (p->pos - t.pos) / dist * std::min(cfg_.thief_speed * dt, dist);
  }
  // Kẻ trộm bỏ chạy đủ xa / quá lâu -> gỡ
  std::erase_if(thieves_, [&](const Thief& t) {
    const CapturePlayer* p = find(t.target);
    return t.fleeing && (!p || glm::distance(t.pos, p->pos) > cfg_.spawn_max_dist_m + 20 || now_s - t.spawned_at > 120);
  });

  // ---- 3. bị giữ / giải cứu
  for (auto& p : players) {
    auto& s = *p.status;
    if (s.state != CaptureState::Captured && s.state != CaptureState::RescueInProgress) continue;
    if (unix_now >= s.hold_until) {  // hết thời hạn: tự được thả về chỗ an toàn
      s.state = CaptureState::Normal;
      s.immune_until = unix_now + static_cast<int64_t>(cfg_.immunity_s);
      s.rescuer = 0;
      s.rescue_progress = 0;
      if (p.move_pos) *p.move_pos = release_;
      ev.push_back({CaptureEvent::Type::Released, p.id, 0, s.capture_id});
      continue;
    }
    // Giữ trong bán kính khu giữ (vẫn đi lại / chat được)
    if (p.move_pos && glm::distance(*p.move_pos, hold_) > cfg_.hold_radius_m)
      *p.move_pos = hold_ + glm::normalize(*p.move_pos - hold_) * cfg_.hold_radius_m;
  }
  // Người cứu: bấm tương tác ở gần khu giữ. Mỗi lần bị bắt chỉ MỘT người cứu tại một thời điểm.
  for (auto& r : players) {
    if (!r.interact_pressed || !r.online || !r.alive || r.status->state != CaptureState::Normal) continue;
    if (glm::distance(r.pos, hold_) > cfg_.rescue_range_m + cfg_.hold_radius_m) continue;
    for (auto& v : players) {
      if (v.id == r.id || v.status->state != CaptureState::Captured) continue;
      v.status->state = CaptureState::RescueInProgress;
      v.status->rescuer = r.id;
      v.status->rescue_progress = 0;
      ev.push_back({CaptureEvent::Type::RescueStarted, v.id, r.id, v.status->capture_id});
      break;  // một lần bấm cứu một người
    }
  }
  for (auto& v : players) {
    auto& s = *v.status;
    if (s.state != CaptureState::RescueInProgress) continue;
    CapturePlayer* r = find(s.rescuer);
    // Người cứu rớt mạng / chết / rời khỏi khu -> gián đoạn, phải bắt đầu lại
    if (!r || !r->online || !r->alive || glm::distance(r->pos, hold_) > cfg_.rescue_range_m + cfg_.hold_radius_m ||
        r->status->state != CaptureState::Normal) {
      ev.push_back({CaptureEvent::Type::RescueInterrupted, v.id, s.rescuer, s.capture_id});
      s.state = CaptureState::Captured;
      s.rescuer = 0;
      s.rescue_progress = 0;
      continue;
    }
    s.rescue_progress += dt;
    if (s.rescue_progress >= cfg_.rescue_channel_s) {
      s.state = CaptureState::Normal;
      s.immune_until = unix_now + static_cast<int64_t>(cfg_.immunity_s);
      ev.push_back({CaptureEvent::Type::Rescued, v.id, r->id, s.capture_id});
      s.rescuer = 0;
      s.rescue_progress = 0;
    }
  }
  return ev;
}

// ============================================================================ companion
std::optional<std::string> tick_companion(Companion& c, glm::vec2 owner_pos, const CompanionConfig& cfg, double now_s, float dt,
                                          const std::vector<Thief>& thieves, const std::vector<glm::vec2>& food_spots) {
  const glm::vec2 d = owner_pos - c.pos;
  const float dist = glm::length(d);
  if (dist > 25.0f) c.pos = owner_pos - glm::vec2(cfg.follow_distance_m, 0);  // tụt quá xa (dịch chuyển): nhảy về cạnh chủ
  else if (dist > cfg.follow_distance_m) {
    c.pos += d / dist * std::min(cfg.speed * dt, dist - cfg.follow_distance_m);
    c.yaw = std::atan2(d.x, d.y);
  }
  if (now_s - c.last_alert < cfg.alert_cooldown_s) return std::nullopt;
  auto dir = [](glm::vec2 v) {
    static const char* n[] = {"bac", "dong bac", "dong", "dong nam", "nam", "tay nam", "tay", "tay bac"};
    const float a = std::atan2(v.x, -v.y);  // 0 = bắc (−Z)
    return n[static_cast<int>(std::lround((a < 0 ? a + 6.2831853f : a) / 0.785398f)) % 8];
  };
  for (const auto& t : thieves)
    if (!t.fleeing && glm::distance(t.pos, owner_pos) <= cfg.danger_alert_m) {
      c.last_alert = now_s;
      return std::string("companion: gru! nguoi la o huong ") + dir(t.pos - owner_pos);
    }
  const glm::vec2* best = nullptr;
  for (const auto& f : food_spots)
    if (glm::distance(f, owner_pos) <= cfg.food_alert_m && (!best || glm::distance(f, owner_pos) < glm::distance(*best, owner_pos))) best = &f;
  if (best && glm::distance(*best, owner_pos) > 3.0f) {
    c.last_alert = now_s;
    return std::string("companion: ngui thay do an o huong ") + dir(*best - owner_pos);
  }
  return std::nullopt;
}

// ============================================================================ kỹ năng
const char* to_string(Skill s) {
  static const char* n[] = {"speed", "stamina", "strength", "scent"};
  return n[static_cast<int>(s)];
}
int SkillSet::level(Skill s, const SkillConfig& c) const {
  return std::min(c.max_level, static_cast<int>(std::sqrt(std::max(0.0f, xp[static_cast<int>(s)]) / c.xp_per_level)));
}
void SkillSet::add_xp(Skill s, float amount, const SkillConfig& c) {
  if (!(amount > 0) || !std::isfinite(amount)) return;
  const float cap = c.xp_per_level * static_cast<float>(c.max_level * c.max_level);
  auto& x = xp[static_cast<int>(s)];
  x = std::min(cap, x + amount);
}
float speed_bonus(const SkillSet& s, const SkillConfig& c) { return 1.0f + c.speed_bonus_per_level * static_cast<float>(s.level(Skill::Speed, c)); }
float strength_bonus(const SkillSet& s, const SkillConfig& c, bool vs_player) {
  return vs_player ? 1.0f : 1.0f + c.strength_bonus_per_level * static_cast<float>(s.level(Skill::Strength, c));
}
float scent_bonus(const SkillSet& s, const SkillConfig& c) { return 1.0f + c.scent_bonus_per_level * static_cast<float>(s.level(Skill::Scent, c)); }

// ============================================================================ nhiệm vụ
std::vector<const QuestDef*> QuestBook::on_event(const std::vector<QuestDef>& defs, QuestObjective obj, float amount, int64_t unix_now) {
  std::vector<const QuestDef*> done;
  for (const auto& d : defs) {
    if (d.objective != obj || !(amount > 0)) continue;
    auto& p = q[d.id];
    const int64_t period = d.daily ? utc_day(unix_now) : 0;
    if (p.period == period) continue;  // đã xong kỳ này
    if (p.started >= 0 && static_cast<double>(unix_now) - p.started > d.timeout_s) {  // quá hạn: làm lại từ đầu
      p.progress = 0;
      p.started = -1;
    }
    if (p.started < 0) p.started = static_cast<double>(unix_now);
    p.progress += amount;
    if (p.progress >= d.count) {
      p.period = period;
      p.progress = 0;
      p.started = -1;
      p.claimed = false;
      done.push_back(&d);
    }
  }
  return done;
}

// ============================================================================ lưu/nạp
namespace {
// Đọc an toàn: sai kiểu / thiếu -> giá trị mặc định (state đã lưu có thể hỏng; KHÔNG được ném exception làm sập server)
template <class T>
T num_or(const nlohmann::json& j, const char* k, T def) {
  if (!j.is_object() || !j.contains(k) || !j[k].is_number()) return def;
  const double v = j[k].get<double>();
  return std::isfinite(v) ? static_cast<T>(v) : def;
}
bool bool_or(const nlohmann::json& j, const char* k, bool def) { return j.contains(k) && j[k].is_boolean() ? j[k].get<bool>() : def; }
}  // namespace

void to_json(nlohmann::json& j, const CaptureStatus& s) {
  // Chỉ lưu trạng thái bền: Threatened/RescueInProgress được hạ về Normal/Captured khi nạp lại.
  j = {{"state", static_cast<int>(s.state)}, {"id", s.capture_id}, {"holdUntil", s.hold_until}, {"immuneUntil", s.immune_until}};
}
void from_json_safe(const nlohmann::json& j, CaptureStatus& s) {
  s = {};
  if (!j.is_object()) return;
  const int st = num_or(j, "state", 0);
  s.state = st >= 0 && st <= 3 ? static_cast<CaptureState>(st) : CaptureState::Normal;
  s.capture_id = num_or<uint32_t>(j, "id", 0);
  s.hold_until = num_or<int64_t>(j, "holdUntil", 0);
  s.immune_until = num_or<int64_t>(j, "immuneUntil", 0);
}
nlohmann::json skills_to_json(const SkillSet& s) {
  nlohmann::json j = nlohmann::json::object();
  for (int i = 0; i < static_cast<int>(Skill::Count); ++i) j[to_string(static_cast<Skill>(i))] = s.xp[i];
  return j;
}
void skills_from_json(const nlohmann::json& j, SkillSet& s, const SkillConfig& c) {
  s = {};
  if (!j.is_object()) return;
  for (int i = 0; i < static_cast<int>(Skill::Count); ++i) {
    const float v = num_or(j, to_string(static_cast<Skill>(i)), 0.0f);
    s.add_xp(static_cast<Skill>(i), v, c);  // đi qua add_xp -> kẹp trần, bỏ NaN/âm
  }
}
nlohmann::json quests_to_json(const QuestBook& b) {
  nlohmann::json j = nlohmann::json::object();
  for (const auto& [id, p] : b.q) j[id] = {{"p", p.progress}, {"s", p.started}, {"d", p.period}, {"c", p.claimed}};
  return j;
}
void quests_from_json(const nlohmann::json& j, QuestBook& b, const std::vector<QuestDef>& defs) {
  b = {};
  if (!j.is_object()) return;
  for (const auto& d : defs)
    if (j.contains(d.id) && j[d.id].is_object()) {
      const auto& e = j[d.id];
      QuestProgress p;
      p.progress = std::clamp(num_or(e, "p", 0.0f), 0.0f, d.count);
      p.started = num_or(e, "s", -1.0);
      p.period = num_or<int64_t>(e, "d", -1);
      p.claimed = bool_or(e, "c", false);
      b.q[d.id] = p;
    }
}

}  // namespace pb::sim
