// Phase 14 — gameplay nâng cao trên server: bắt chó/giải cứu, companion, kỹ năng, nhiệm vụ, lãnh thổ.
// Mọi quyết định ở server; Backend chỉ lưu (thưởng idempotent, thông báo, chủ lãnh thổ).
#include <chrono>
#include <cmath>
#include <format>

#include <glm/geometric.hpp>
#include <nlohmann/json.hpp>

#include "core/log.hpp"
#include "server/game_server.hpp"

namespace pb::server {
namespace {
glm::vec2 xz(glm::vec3 v) { return {v.x, v.z}; }
int64_t unix_now() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
const char* direction(glm::vec2 v) {
  static const char* n[] = {"bắc", "đông bắc", "đông", "đông nam", "nam", "tây nam", "tây", "tây bắc"};
  const float a = std::atan2(v.x, -v.y);
  return n[static_cast<int>(std::lround((a < 0 ? a + 6.2831853f : a) / 0.785398f)) % 8];
}
}  // namespace

void GameServer::event(Player& p, uint16_t code, const std::string& text) {
  if (p.online && p.peer) send(p.peer, net::PacketType::Event, net::write(net::EventMsg{code, text}), true);
}

void GameServer::init_advanced() {
  // Khu giữ chó bị bắt: điểm trú ẩn trong khu KHÔNG an toàn xa điểm spawn nhất; khu thả: điểm spawn (an toàn).
  glm::vec2 hold{world::to_m(world_.size_x) * 0.5f, world::to_m(world_.size_z) * 0.5f};
  float best = -1;
  for (const auto& c : world_.chunks)
    for (const auto& poi : c.pois)
      if (poi.kind == world::PoiKind::Shelter && world_.zone_at(poi.x, poi.z) != world::ZoneRule::Safe) {
        const glm::vec2 p{world::to_m(poi.x), world::to_m(poi.z)};
        if (glm::distance(p, spawn_) > best) {
          best = glm::distance(p, spawn_);
          hold = p;
        }
      }
  col_->resolve(hold, 0.5f, world_.height_at(hold.x, hold.y), 0.3f);
  capture_ = std::make_unique<sim::CaptureSystem>(adv_.capture, hold, spawn_, world_.seed ^ 0xCA97u);
  log::info("server", "gameplay nâng cao: khu giữ ({:.0f}, {:.0f}), {} nhiệm vụ, kẻ trộm {}", hold.x, hold.y, adv_.quests.size(),
            adv_.capture.enabled ? "BẬT" : "tắt");
}

void GameServer::grant_bones(Player& p, int amount, const std::string& reason, const std::string& ref_key) {
  if (amount <= 0) return;
  event(p, 60, std::format("+{} xương ({})", amount, reason));
  if (!backend_.enabled()) return;  // DEV: không có ví
  // refKey idempotent: cùng phần thưởng gửi lại / hai lần nhận -> Backend chỉ cộng một lần
  backend_.submit({BackendJob::Reward, p.id, p.char_id, "POST", std::format("/internal/wallet/{}/earn", p.user_id),
                   nlohmann::json{{"amount", amount}, {"reason", reason}, {"refKey", ref_key}}.dump()});
}

void GameServer::quest_event(Player& p, sim::QuestObjective obj, float amount) {
  for (const auto* q : p.quests.on_event(adv_.quests, obj, amount, unix_now())) {
    auto& qp = p.quests.q[q->id];
    event(p, 61, "Nhiệm vụ hoàn thành: " + q->title);
    grant_bones(p, q->reward_bones, "quest", std::format("quest:{}:{}:{}", p.char_id, q->id, qp.period));
    qp.claimed = true;
  }
}

void GameServer::try_recruit_companion(Player& p) {
  const auto& cc = adv_.companion;
  if (!cc.enabled || p.has_companion) return;  // tối đa MỘT companion (chống nhân bản)
  for (const auto& n : npcs_->npcs()) {
    if (n.kind != sim::NpcKind::Dog || n.pers != sim::Personality::Friendly || n.sv.life != sim::LifeState::Alive) continue;
    if (glm::distance(xz(n.motor.pos), xz(p.motor.pos)) > gc_.interact_range_m + 1.0f) continue;
    const int slot = p.inv.find(cc.recruit_item);
    if (slot < 0) {
      event(p, 62, "Chó hoang nhìn bạn... (cần " + cc.recruit_item + " để làm quen)");
      return;
    }
    p.inv.remove(slot, 1);
    p.has_companion = true;
    p.companion = {p.id, xz(n.motor.pos), 0, -1e9};
    event(p, 63, "Bạn có bạn đồng hành mới! Nó sẽ cảnh báo kẻ lạ và đánh hơi đồ ăn.");
    request_save(p, "companion");
    return;
  }
}

bool GameServer::attack_thief(Player& p, float range) {
  for (const auto& t : capture_->thieves()) {
    if (t.fleeing || glm::distance(t.pos, xz(p.motor.pos)) > range) continue;
    if (!sim::spend_stamina(p.sv, gc_.survival, gc_.combat.bite_stamina)) return true;
    const float dmg = gc_.combat.bite_damage * sim::strength_bonus(p.skills, adv_.skills, false);  // kỹ năng chỉ tăng sát thương lên NPC
    capture_->hit_thief(t.id, dmg);
    p.skills.add_xp(sim::Skill::Strength, 5, adv_.skills);
    event(p, 2, std::format("bite: thief -{:.0f}", dmg));
    return true;
  }
  return false;
}

void GameServer::notify_teammates(const Player& victim, const std::string& text) {
  for (auto& [id, o] : players_) {
    if (id == victim.id || !o.online || !o.loaded || !victim.teammates.contains(o.user_id)) continue;
    // Manh mối: hướng + khoảng cách tới khu giữ (tính từ vị trí người nhận) — không lộ cho người ngoài nhóm
    const glm::vec2 d = capture_->hold_point() - xz(o.motor.pos);
    event(o, 50, std::format("{} — dấu vết ở hướng {}, khoảng {:.0f} m", text, direction(d), glm::length(d)));
  }
}

void GameServer::on_capture_event(const sim::CaptureEvent& e) {
  using T = sim::CaptureEvent::Type;
  Player* p = player_by_id(e.player);
  if (!p) return;
  auto notify_backend = [&](const char* state) {
    if (!backend_.enabled()) return;
    const auto d = world_.district_at(static_cast<world::cm>(capture_->hold_point().x * 100), static_cast<world::cm>(capture_->hold_point().y * 100));
    backend_.submit({BackendJob::Notify, p->id, p->char_id, "POST", "/internal/events/capture",
                     nlohmann::json{{"userNumericId", std::to_string(p->user_id)}, {"captureId", e.capture_id}, {"state", state},
                                    {"district", world::to_string(d)}}.dump()});
  };
  switch (e.type) {
    case T::Warning:
      event(*p, 40, "CẢNH BÁO: kẻ trộm chó đang tới gần! Chạy (Shift), vào khu an toàn hoặc cắn (J)");
      break;
    case T::Escaped:
      event(*p, 41, "Bạn đã thoát khỏi kẻ trộm!");
      quest_event(*p, sim::QuestObjective::EscapeThief, 1);
      break;
    case T::ThiefDefeated:
      event(*p, 42, "Kẻ trộm bỏ chạy!");
      break;
    case T::Captured:
      event(*p, 43, std::format("Bạn bị bắt! Chờ đồng đội giải cứu (tự được thả sau tối đa {}). Không mất đồ.",
                                adv_.capture.hold_max_s >= 60 ? std::format("{:.0f} phút", adv_.capture.hold_max_s / 60.0)
                                                              : std::format("{:.0f} giây", adv_.capture.hold_max_s)));
      notify_teammates(*p, std::format("Đồng đội #{} bị bắt", p->id));
      notify_backend("CAPTURED");
      request_save(*p, "captured");  // lưu ngay: thoát game/rớt server không xóa được trạng thái bị bắt
      break;
    case T::RescueStarted:
      event(*p, 44, std::format("Người chơi #{} đang giải cứu bạn...", e.other));
      if (Player* r = player_by_id(e.other)) event(*r, 44, std::format("Đang giải cứu #{} — đứng yên gần chuồng", p->id));
      break;
    case T::RescueInterrupted:
      event(*p, 45, "Cuộc giải cứu bị gián đoạn");
      if (Player* r = player_by_id(e.other)) event(*r, 45, "Giải cứu bị gián đoạn (bạn đã rời khỏi chuồng)");
      break;
    case T::Rescued:
      event(*p, 46, "Bạn đã được giải cứu!");
      notify_backend("RESCUED");
      if (Player* r = player_by_id(e.other)) {
        event(*r, 47, std::format("Giải cứu thành công #{}!", p->id));
        // Mỗi lần bị bắt chỉ có MỘT phần thưởng: khóa theo (nạn nhân, lần bắt)
        grant_bones(*r, adv_.capture.rescue_reward_bones, "rescue", std::format("rescue:{}:{}", p->char_id, e.capture_id));
        quest_event(*r, sim::QuestObjective::Rescue, 1);
      }
      request_save(*p, "rescued");
      break;
    case T::Released:
      event(*p, 48, "Hết thời gian giữ — bạn được thả ở khu an toàn");
      notify_backend("RELEASED");
      request_save(*p, "released");
      break;
  }
}

void GameServer::tick_advanced(double dt) {
  const int64_t un = unix_now();
  std::vector<sim::CapturePlayer> cps;
  std::vector<Player*> owners;
  std::vector<glm::vec2> moved;
  cps.reserve(players_.size());
  moved.reserve(players_.size());
  for (auto& [id, p] : players_) {
    if (!p.loaded) continue;
    const auto zone = world_.zone_at(static_cast<world::cm>(p.motor.pos.x * 100), static_cast<world::cm>(p.motor.pos.z * 100));
    moved.push_back(xz(p.motor.pos));
    owners.push_back(&p);
    cps.push_back({p.id, xz(p.motor.pos), p.sv.life == sim::LifeState::Alive, p.online, zone == world::ZoneRule::Safe, now_ - p.joined_at,
                   p.interact_this_tick, &p.cap, nullptr});
    p.interact_this_tick = false;
  }
  for (size_t i = 0; i < cps.size(); ++i) cps[i].move_pos = &moved[i];
  const auto zone_ok = [&](glm::vec2 q) {
    return world_.zone_at(static_cast<world::cm>(q.x * 100), static_cast<world::cm>(q.y * 100)) != world::ZoneRule::Safe;
  };
  const auto events = capture_->tick(cps, now_, un, static_cast<float>(wsim_->clock().hour()), static_cast<float>(dt), zone_ok);
  for (size_t i = 0; i < owners.size(); ++i) {
    Player& p = *owners[i];
    if (moved[i] != xz(p.motor.pos)) {  // hệ thống dịch chuyển (vào chuồng / được thả / giữ trong bán kính)
      p.motor.pos = {moved[i].x, world_.height_at(moved[i].x, moved[i].y), moved[i].y};
      p.motor.vel = glm::vec2(0.0f);
    }
  }
  for (const auto& e : events) on_capture_event(e);

  // Companion + kỹ năng + nhiệm vụ "đi đường"
  std::vector<glm::vec2> food;
  for (const auto& n : resources_)
    if (n.kind == world::PoiKind::Food && n.remaining > 0) food.push_back(n.pos);
  for (auto& [id, p] : players_) {
    if (!p.online || !p.loaded) continue;
    if (p.has_companion)
      if (auto a = sim::tick_companion(p.companion, xz(p.motor.pos), adv_.companion, now_, static_cast<float>(dt), capture_->thieves(), food))
        event(p, 64, *a);
    const float moved_m = glm::distance(xz(p.motor.pos), p.last_xp_pos);
    if (moved_m > 1.0f && moved_m < 20.0f) {  // bỏ qua dịch chuyển (tp/chuồng) — không cày xp bằng cách bị bắt
      p.skills.add_xp(sim::Skill::Speed, moved_m * 0.1f, adv_.skills);
      quest_event(p, sim::QuestObjective::Travel, moved_m);
    }
    if (moved_m > 1.0f) p.last_xp_pos = xz(p.motor.pos);
    if (p.sprinting) p.skills.add_xp(sim::Skill::Stamina, static_cast<float>(dt), adv_.skills);
    if (p.prev_buttons & net::kSniff) p.skills.add_xp(sim::Skill::Scent, static_cast<float>(dt) * 0.5f, adv_.skills);
  }
  if (now_ >= next_territory_eval_) {
    next_territory_eval_ = now_ + cfg_.territory_eval_interval_s;
    tick_territory();
  }
}

void GameServer::tick_territory() {
  if (!backend_.enabled()) return;
  // Khu = ô quận (district cell). Đếm thành viên bang online, còn sống, không bị bắt trong mỗi khu.
  std::unordered_map<int, std::unordered_map<std::string, int>> counts;
  for (auto& [id, p] : players_) {
    if (!p.online || !p.loaded || p.guild_id.empty() || p.cap.state != sim::CaptureState::Normal || p.sv.life != sim::LifeState::Alive) continue;
    const int cx = std::clamp(static_cast<int>(p.motor.pos.x * 100 * world_.dcells_x / std::max<world::cm>(1, world_.size_x)), 0, world_.dcells_x - 1);
    const int cz = std::clamp(static_cast<int>(p.motor.pos.z * 100 * world_.dcells_z / std::max<world::cm>(1, world_.size_z)), 0, world_.dcells_z - 1);
    counts[cz * world_.dcells_x + cx][p.guild_id]++;
  }
  std::unordered_map<std::string, double> next;
  for (const auto& [zone, guilds] : counts) {
    if (guilds.size() != 1) continue;  // có bang khác hiện diện -> đang tranh chấp, không ai tích lũy
    const auto& [gid, n] = *guilds.begin();
    if (n < cfg_.territory_min_members) continue;
    const std::string key = std::format("{}:{}", zone, gid);
    const double held = (territory_hold_.contains(key) ? territory_hold_[key] : 0) + cfg_.territory_eval_interval_s;
    if (held >= cfg_.territory_hold_s && now_ >= territory_cooldown_[zone]) {
      territory_cooldown_[zone] = now_ + 60;  // không gửi dồn dập; Backend có cooldown/bảo hộ riêng
      backend_.submit({BackendJob::Territory, 0, 0, "POST", "/internal/territory/claim",
                       nlohmann::json{{"zoneId", zone}, {"guildId", gid}, {"serverId", cfg_.server_id}, {"members", n}}.dump()});
      continue;  // reset
    }
    next[key] = held;
  }
  territory_hold_ = std::move(next);
}

}  // namespace pb::server
