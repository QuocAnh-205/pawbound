#pragma once
// Phase 14 — Gameplay nâng cao, AUTHORITATIVE trên server: bắt chó (kẻ trộm), giải cứu, companion, kỹ năng, nhiệm vụ.
// Module thuần (không mạng, không DB): server gọi từng tick; mọi thông số từ data/gameplay/advanced.json.
#include <cstdint>
#include <functional>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/vec2.hpp>
#include <nlohmann/json_fwd.hpp>

#include "core/config_reader.hpp"

namespace pb::sim {

// ============================================================================ cấu hình
struct CaptureConfig {
  bool enabled = true;
  std::vector<int> active_hours{20, 21, 22, 23, 0, 1, 2, 3, 4, 5};  // giờ thế giới kẻ trộm hoạt động
  float spawn_chance_per_min = 0.25f;  // mỗi người chơi đủ điều kiện, mỗi phút
  float spawn_min_dist_m = 30, spawn_max_dist_m = 45;
  int max_thieves = 3;
  float min_session_s = 300;   // người mới vào phiên < 5 phút: không bị nhắm (bảo vệ người chơi mới)
  float warn_distance_m = 18;  // kẻ trộm vào tầm này -> CẢNH BÁO
  float min_warning_s = 3;     // luôn có ít nhất N giây phản ứng sau cảnh báo
  float give_up_s = 12;        // đuổi quá lâu -> bỏ cuộc (người chơi thoát)
  float escape_distance_m = 28;
  float grab_distance_m = 1.6f;
  float thief_speed = 4.6f;    // nhanh hơn chạy nước kiệu, chậm hơn nước rút
  float thief_hp = 40;
  float hold_max_s = 600;      // tối đa 10 phút (KHÔNG khóa người chơi lâu ngoài đời)
  float hold_radius_m = 4;     // trong lúc chờ vẫn đi lại, chat được trong khu giữ
  float rescue_channel_s = 4;
  float rescue_range_m = 2.5f;
  float immunity_s = 1800;     // sau khi thoát/được cứu/được thả: 30 phút không bị bắt lại (chống kill loop)
  int rescue_reward_bones = 80;
};

struct CompanionConfig {
  bool enabled = true;
  std::string recruit_item = "meat_bone";  // cho ăn 1 món này để có bạn đồng hành
  float follow_distance_m = 2.0f;
  float speed = 6.0f;
  float danger_alert_m = 30;  // phát hiện kẻ trộm xa hơn tầm cảnh báo thường
  float food_alert_m = 30;
  float alert_cooldown_s = 20;
};

struct SkillConfig {
  int max_level = 5;
  float xp_per_level = 100;            // level = floor(sqrt(xp / xp_per_level)), tối đa max_level
  float speed_bonus_per_level = 0.01f;  // tối đa +5% — giới hạn chênh lệch người mới/cũ
  float scent_bonus_per_level = 0.05f;
  float strength_bonus_per_level = 0.03f;  // CHỈ áp lên NPC/kẻ trộm, KHÔNG áp lên người chơi khác (cân bằng PvP)
};

enum class QuestObjective : uint8_t { Forage, Drink, Travel, Rescue, EscapeThief };
struct QuestDef {
  std::string id, title;
  QuestObjective objective = QuestObjective::Forage;
  float count = 1;
  int reward_bones = 0;
  float timeout_s = 1800;  // tính từ lúc có tiến độ đầu tiên
  bool daily = true;       // làm lại mỗi ngày thật (UTC)
};

struct AdvancedConfig {
  CaptureConfig capture;
  CompanionConfig companion;
  SkillConfig skills;
  std::vector<QuestDef> quests;
};

AdvancedConfig load_advanced_config(const std::filesystem::path& path, ConfigDiagnostics& diag);
AdvancedConfig parse_advanced_config(std::string_view text, ConfigDiagnostics& diag);

// ============================================================================ bắt chó / giải cứu
enum class CaptureState : uint8_t { Normal = 0, Threatened = 1, Captured = 2, RescueInProgress = 3 };
const char* to_string(CaptureState s);

struct CaptureStatus {
  CaptureState state = CaptureState::Normal;
  uint32_t capture_id = 0;
  uint32_t thief = 0;          // id kẻ trộm đang nhắm (Threatened)
  double threatened_at = 0;    // giờ server (giây) lúc cảnh báo
  int64_t hold_until = 0;      // unix giây: hết giờ giữ -> tự được thả (tính cả khi offline -> relog không có lợi)
  int64_t immune_until = 0;    // unix giây
  uint32_t rescuer = 0;        // id người chơi đang cứu
  float rescue_progress = 0;
};

struct Thief {
  uint32_t id = 0;
  uint32_t target = 0;
  glm::vec2 pos{0.0f};
  float hp = 40;
  double spawned_at = 0;
  bool fleeing = false;
};

// Sự kiện để server gửi cho người chơi / Backend
struct CaptureEvent {
  enum class Type : uint8_t { Warning, Escaped, Captured, RescueStarted, RescueInterrupted, Rescued, Released, ThiefDefeated };
  Type type;
  uint32_t player = 0;
  uint32_t other = 0;  // rescuer / thief
  uint32_t capture_id = 0;
};
const char* to_string(CaptureEvent::Type t);

// Dữ liệu tối thiểu về một người chơi mà hệ thống cần (server điền mỗi tick).
struct CapturePlayer {
  uint32_t id = 0;
  glm::vec2 pos{0.0f};
  bool alive = true;
  bool online = true;
  bool in_safe_zone = false;
  double session_s = 0;       // thời gian trong phiên hiện tại
  bool interact_pressed = false;
  CaptureStatus* status = nullptr;
  glm::vec2* move_pos = nullptr;  // hệ thống có thể dịch chuyển người chơi (vào khu giữ / về chỗ an toàn)
};

class CaptureSystem {
 public:
  CaptureSystem(const CaptureConfig& cfg, glm::vec2 hold_point, glm::vec2 release_point, uint64_t seed);
  // world_hour: giờ thế giới; zone_ok(pos): kẻ trộm chỉ hoạt động ở khu không an toàn.
  std::vector<CaptureEvent> tick(std::vector<CapturePlayer>& players, double now_s, int64_t unix_now, float world_hour, float dt,
                                 const std::function<bool(glm::vec2)>& zone_ok);
  // Người chơi đánh kẻ trộm (server đã kiểm tầm đánh). Trả true nếu trúng.
  bool hit_thief(uint32_t thief_id, float damage);
  [[nodiscard]] const std::vector<Thief>& thieves() const { return thieves_; }
  [[nodiscard]] glm::vec2 hold_point() const { return hold_; }
  // Khi người chơi rời game: kẻ trộm đang nhắm họ bỏ đi; nếu đang được cứu thì tiến độ mất (rescuer rời) — xử lý ở tick.
  void on_player_removed(uint32_t player);
  // Áp trạng thái đã lưu khi người chơi vào lại (chống relog-thoát): còn hạn giữ -> về khu giữ.
  void restore(CapturePlayer& p, int64_t unix_now);
  uint32_t next_capture_id() { return ++capture_seq_; }
  void set_enabled(bool on) { cfg_.enabled = on; }

 private:
  CaptureConfig cfg_;
  glm::vec2 hold_, release_;
  std::vector<Thief> thieves_;
  uint32_t thief_seq_ = 0, capture_seq_ = 0;
  uint64_t rng_;
  double next_spawn_roll_ = 0;
  float frand();
};

// ============================================================================ companion
struct Companion {
  uint32_t owner = 0;
  glm::vec2 pos{0.0f};
  float yaw = 0;
  double last_alert = -1e9;
};
// Trả hướng/sự kiện cảnh báo (rỗng nếu không có gì). Không bao giờ tấn công người chơi.
std::optional<std::string> tick_companion(Companion& c, glm::vec2 owner_pos, const CompanionConfig& cfg, double now_s, float dt,
                                          const std::vector<Thief>& thieves, const std::vector<glm::vec2>& food_spots);

// ============================================================================ kỹ năng
enum class Skill : uint8_t { Speed, Stamina, Strength, Scent, Count };
const char* to_string(Skill s);
struct SkillSet {
  float xp[static_cast<int>(Skill::Count)]{};
  [[nodiscard]] int level(Skill s, const SkillConfig& c) const;
  void add_xp(Skill s, float amount, const SkillConfig& c);  // xp bị kẹp ở mức đủ max_level
};
float speed_bonus(const SkillSet& s, const SkillConfig& c);             // nhân vào tốc độ, ≤ 1 + max_level × bonus
float strength_bonus(const SkillSet& s, const SkillConfig& c, bool vs_player);  // vs_player -> luôn 1.0
float scent_bonus(const SkillSet& s, const SkillConfig& c);

// ============================================================================ nhiệm vụ
struct QuestProgress {
  float progress = 0;
  double started = -1;  // unix giây; -1 = chưa bắt đầu
  int64_t period = -1;  // ngày (UTC) đã hoàn thành gần nhất
  bool claimed = false; // phần thưởng đã gửi Backend cho period này
};
struct QuestBook {
  std::unordered_map<std::string, QuestProgress> q;
  // Cộng tiến độ; trả danh sách quest VỪA hoàn thành (mỗi quest tối đa một lần mỗi kỳ).
  std::vector<const QuestDef*> on_event(const std::vector<QuestDef>& defs, QuestObjective obj, float amount, int64_t unix_now);
};
inline int64_t utc_day(int64_t unix_s) { return unix_s / 86400; }

// ============================================================================ lưu/nạp (JSON trong state nhân vật)
void to_json(nlohmann::json& j, const CaptureStatus& s);
void from_json_safe(const nlohmann::json& j, CaptureStatus& s);
nlohmann::json skills_to_json(const SkillSet& s);
void skills_from_json(const nlohmann::json& j, SkillSet& s, const SkillConfig& c);
nlohmann::json quests_to_json(const QuestBook& b);
void quests_from_json(const nlohmann::json& j, QuestBook& b, const std::vector<QuestDef>& defs);

}  // namespace pb::sim
