#pragma once
// NPC người + chó NPC: spawn theo chunk, state machine theo tính cách, nhận biết (nhìn/nghe/khoảng cách),
// điều hướng bằng waypoint graph, AI LOD và ngân sách tìm đường. Logic thuần — chạy được trên server.
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sim/collision.hpp"
#include "sim/dog_motor.hpp"
#include "sim/gameplay.hpp"
#include "sim/nav.hpp"
#include "world/world_types.hpp"

namespace pb::sim {

enum class NpcKind : uint8_t { Human, Dog };
enum class Personality : uint8_t { Neutral, Friendly, Fearful, Hostile, Vendor, Aggressive };
enum class NpcState : uint8_t {
  Idle, Walk, Wait, Flee, Approach, Feed, Scare, CallHelp, Defend,       // người
  Roam, Sniff, SeekFood, Eat, SeekWater, Drink, Rest, Follow, Growl, Fight  // chó
};
const char* to_string(NpcKind k);
const char* to_string(Personality p);
const char* to_string(NpcState s);

struct NpcConfig {
  std::array<float, world::kDistrictCount> humans_per_chunk{5, 3, 2, 1};
  std::array<float, world::kDistrictCount> dogs_per_chunk{0.5f, 1, 1.5f, 1.5f};
  int active_radius_chunks = 1;
  int max_active = 120;
  float lod_near_m = 30;
  int far_tick_interval = 6;
  int max_path_requests_per_tick = 8;
  int w_friendly = 30, w_fearful = 30, w_hostile = 15, w_neutral = 25;
  float h_walk = 1.3f, h_run = 3.6f, h_sight = 14, h_fov = 140, h_hearing = 18, h_radius = 0.3f;
  float kick_damage = 6, kick_cd = 1.5f, feed_cd = 120, notoriety_wary = 10;
  float d_sight = 12, d_fov = 200, d_hearing = 22;
  int d_aggressive_pct = 30, d_friendly_pct = 30;
  float growl_s = 1.5f, fight_range = 4, follow_s = 30, flee_hp = 0.3f, need_rate_mult = 6;
  uint32_t max_expanded = 3000;
  float stuck_s = 3, goal_timeout_s = 60, roam_radius = 45;
};
NpcConfig load_npc_config(const std::filesystem::path& path, ConfigDiagnostics& diag);

// Góc nhìn của AI về một người chơi (server điền từ trạng thái authoritative).
struct PlayerView {
  uint32_t id = 0;
  glm::vec3 pos{0.0f};
  float yaw = 0;
  SurvivalState* sv = nullptr;
  CombatState* cb = nullptr;
};

struct Noise {
  glm::vec2 pos{0.0f};
  float radius = 10;
  uint32_t source_player = 0;  // 0 = không phải người chơi
};

struct AiEvent {
  enum class Type { FeedPlayer, BitePlayer, KickPlayer, Shout, CallHelp, Growl } type;
  uint32_t npc = 0;
  uint32_t player = 0;
  float value = 0;
};

struct Npc {
  uint32_t id = 0;  // (chunk << 12) | chỉ số — ổn định theo seed
  NpcKind kind = NpcKind::Human;
  Personality pers = Personality::Neutral;
  NpcState state = NpcState::Idle;
  int home_chunk = 0;
  MotorState motor;
  MovementStats stats;
  SurvivalState sv;
  CombatState cb;
  std::vector<uint32_t> path;
  size_t path_i = 0;
  glm::vec2 goal{0.0f};
  bool has_goal = false;
  float state_t = 0, goal_t = 0, stuck_t = 0, feed_cd = 0, kick_cd = 0, lod_accum = 0;
  glm::vec2 stuck_ref{0.0f};
  uint32_t target_player = 0;
  uint64_t rng = 0;
  float last_hp = 100;
  float presence = 0;  // người: có mặt khi presence < mật độ hiện tại (giờ × thời tiết)
  bool alert = false;
};

class NpcManager {
 public:
  NpcManager(const world::WorldData& w, const CollisionWorld& col, NavGraph& nav, const GameplayConfig& gc,
             const NpcConfig& nc, std::vector<ResourceNode>* resources);

  // Một tick server. Trả sự kiện cho lớp gameplay (cho ăn, cắn, đá người chơi, la hét...).
  std::vector<AiEvent> update(float dt, std::span<const PlayerView> players, std::span<const Noise> noises);
  [[nodiscard]] const std::vector<Npc>& npcs() const { return npcs_; }
  [[nodiscard]] std::vector<Npc>& npcs_mut() { return npcs_; }
  [[nodiscard]] Npc* find(uint32_t id);
  [[nodiscard]] const std::unordered_set<int>& active_chunks() const { return active_; }

  struct Stats {
    size_t active = 0, full_ticks = 0, lod_ticks = 0, path_requests = 0, path_deferred = 0, path_failed = 0;
    double last_ms = 0, max_ms = 0;
  };
  [[nodiscard]] const Stats& stats() const { return stats_; }
  [[nodiscard]] std::string debug_line(const Npc& n) const;
  // Thông báo NPC bị người chơi tấn công (để phản ứng: bỏ chạy / tự vệ / gọi giúp).
  void on_attacked(uint32_t npc_id, uint32_t player_id);
  // Mật độ người đi bộ 0..1+ (Phase 9: theo giờ và thời tiết). Thay đổi thì thêm/bớt người theo ngưỡng
  // hiện diện deterministic (người có ngưỡng cao rời đi trước, quay lại sau).
  void set_human_density(float d);
  [[nodiscard]] float human_density() const { return density_; }

 private:
  void sync_chunks(std::span<const PlayerView> players);
  void spawn_chunk(int chunk);
  std::vector<Npc> make_chunk_npcs(int chunk) const;
  void refresh_density();
  void tick_npc(Npc& n, float dt, std::span<const PlayerView> players, std::span<const Noise> noises,
                std::vector<AiEvent>& ev);
  void tick_human(Npc& n, float dt, const PlayerView* p, float dist, bool sees, std::span<const PlayerView> players,
                  std::vector<AiEvent>& ev);
  void tick_dog(Npc& n, float dt, const PlayerView* p, float dist, bool sees, std::vector<AiEvent>& ev);
  bool set_goal(Npc& n, glm::vec2 goal);
  bool random_goal(Npc& n, float radius);
  // Đi theo path; trả true khi tới đích. speed: 0..1 (1 = chạy).
  bool follow_path(Npc& n, float dt, float speed, bool sprint);
  void move_toward(Npc& n, float dt, glm::vec2 target, float speed, bool sprint, float stop_dist);
  void change(Npc& n, NpcState s);

  const world::WorldData& world_;
  const CollisionWorld& col_;
  NavGraph& nav_;
  const GameplayConfig& gc_;
  NpcConfig nc_;
  std::vector<ResourceNode>* resources_;
  std::vector<Npc> npcs_;
  std::unordered_set<int> active_;
  int path_budget_ = 0;
  uint64_t tick_ = 0;
  float density_ = 1.0f, applied_density_ = 1.0f;
  float time_ = 0;
  Stats stats_;
};

}  // namespace pb::sim
