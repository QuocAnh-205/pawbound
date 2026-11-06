#pragma once
// Luật chơi Phase 7: sinh tồn, tài nguyên, inventory, tương tác, chiến đấu, vùng PvP.
// Logic thuần, deterministic, KHÔNG phụ thuộc renderer/UI — chạy được trên dedicated server (Phase 10).
// Mỗi thao tác theo mẫu: yêu cầu -> kiểm tra hợp lệ (trả lý do nếu từ chối) -> áp dụng.
#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "core/config_reader.hpp"
#include "world/world_types.hpp"

namespace pb::sim {

// ============================================================================ cấu hình
struct SurvivalConfig {
  float max_hp = 100, max_hunger = 100, max_thirst = 100, max_stamina = 100, max_rest = 100;
  float hunger_full_to_empty_s = 4800, thirst_full_to_empty_s = 3600, rest_full_to_empty_s = 7200;
  float hp_regen_per_s = 0.25f, starving_hp_loss_per_s = 1.0f / 30, dehydrated_hp_loss_per_s = 1.0f / 20;
  float low_threshold = 20, tired_threshold = 15, tired_speed_mult = 0.8f, thirsty_stamina_max_mult = 0.7f;
  float stamina_regen_per_s = 12, stamina_regen_delay_s = 1.0f, sprint_stamina_per_s = 12, sprint_min_stamina = 15;
  float run_stamina_per_s = 0, jump_stamina = 8;
  float rest_recover_per_s = 1.2f, rest_hp_bonus_per_s = 0.3f, shelter_rest_mult = 2.0f;
  float downed_seconds = 10, respawn_hp_fraction = 0.5f, respawn_drop_consumable_fraction = 0.5f;
};

struct CombatConfig {
  float bite_damage = 12, bite_cooldown_s = 0.8f, bite_stamina = 10, bite_range_m = 1.3f, bite_arc_deg = 90;
  float dodge_iframes_s = 0.3f, dodge_cooldown_s = 1.5f, dodge_stamina = 15;
  int stagger_hits = 3;
  float stagger_window_s = 2.0f, stagger_s = 0.5f, escape_distance_m = 8, escape_time_s = 3;
  int newbie_level = 5;
  float npc_aggression_notoriety = 10, notoriety_decay_per_s = 0.05f;
};

struct ResourceRule {
  int capacity = 3;
  float amount = 35;  // điểm đói/khát hồi mỗi lần dùng
  float regen_s = 240;
};

struct ItemDef {
  std::string id, name, category;
  int max_stack = 1;
  float hunger = 0, thirst = 0;
  bool consumable = false;
};

struct GameplayConfig {
  SurvivalConfig survival;
  CombatConfig combat;
  ResourceRule food{3, 35, 240}, water{10, 30, 60};
  float interact_range_m = 2.0f, sniff_radius_m = 25;
  int inventory_slots = 6;
  std::vector<ItemDef> items;
  [[nodiscard]] const ItemDef* item(const std::string& id) const;
};

GameplayConfig load_gameplay_config(const std::filesystem::path& path, ConfigDiagnostics& diag);
GameplayConfig parse_gameplay_config(std::string_view text, ConfigDiagnostics& diag);

// ============================================================================ sinh tồn
enum class LifeState : uint8_t { Alive, Downed, Dead };

struct SurvivalState {
  float hp = 100, hunger = 100, thirst = 100, stamina = 100, rest = 100;
  LifeState life = LifeState::Alive;
  float downed_timer = 0;
  float stamina_idle = 0;  // thời gian kể từ lần tiêu thể lực gần nhất
  bool resting = false;
};

struct Activity {
  bool moving = false;
  bool sprinting = false;
  bool resting = false;
  bool in_shelter = false;
};

void tick_survival(SurvivalState& s, const SurvivalConfig& c, const Activity& a, float dt);
// Tiêu thể lực cho một hành động; false nếu không đủ (không trừ).
bool spend_stamina(SurvivalState& s, const SurvivalConfig& c, float amount);
[[nodiscard]] float stamina_max(const SurvivalState& s, const SurvivalConfig& c);
[[nodiscard]] float speed_multiplier(const SurvivalState& s, const SurvivalConfig& c);
// Bắt đầu nước rút cần >= sprint_min_stamina; đang nước rút thì tiếp tục tới khi cạn.
[[nodiscard]] bool can_sprint(const SurvivalState& s, const SurvivalConfig& c, bool currently_sprinting);
// Gây sát thương; HP về 0 -> Downed (không chết ngay, không mất nhân vật).
void apply_damage(SurvivalState& s, float amount);

// ============================================================================ inventory
struct ItemStack {
  std::string item;
  int count = 0;
  [[nodiscard]] bool empty() const { return count <= 0 || item.empty(); }
};

class Inventory {
 public:
  explicit Inventory(int slots = 6) : slots_(static_cast<size_t>(slots)) {}
  // Thêm; trả số lượng KHÔNG thêm được (đầy). Item lạ -> không thêm gì.
  int add(const GameplayConfig& cfg, const std::string& item, int count);
  // Bớt từ một ô; false nếu không đủ.
  bool remove(int slot, int count);
  [[nodiscard]] int count_of(const std::string& item) const;
  [[nodiscard]] const std::vector<ItemStack>& slots() const { return slots_; }
  [[nodiscard]] int find(const std::string& item) const;
  // Thao tác có id (idempotent): áp dụng lặp cùng id bị bỏ qua -> chống nhân bản do gửi lại/lag.
  bool apply_once(uint64_t op_id);
  // Kiểm tra toàn vẹn: item tồn tại, 0 < count <= max_stack.
  [[nodiscard]] std::string validate(const GameplayConfig& cfg) const;
  [[nodiscard]] std::string to_json() const;
  static std::optional<Inventory> from_json(const GameplayConfig& cfg, std::string_view json, std::string& error);

 private:
  std::vector<ItemStack> slots_;
  std::unordered_set<uint64_t> applied_;
  std::deque<uint64_t> applied_order_;
};

// ============================================================================ tài nguyên + tương tác
struct ResourceNode {
  uint32_t id = 0;  // id POI ổn định
  world::PoiKind kind = world::PoiKind::Water;
  glm::vec2 pos{0.0f};
  int capacity = 1, remaining = 1;
  float amount = 30;
  float regen_s = 60;
  float regen_timer = 0;
};

std::vector<ResourceNode> make_resource_nodes(const world::WorldData& w, const GameplayConfig& cfg);
void tick_resources(std::vector<ResourceNode>& nodes, float dt);

enum class InteractResult { Ok, OutOfRange, Depleted, NotAlive, Full, NoTarget, InventoryFull, UnknownItem };
const char* to_string(InteractResult r);

// Ăn/uống trực tiếp tại nguồn. Kiểm tra khoảng cách phía "server": vị trí là của server, không tin client.
InteractResult consume_at(ResourceNode& node, glm::vec2 actor_pos, SurvivalState& s, const GameplayConfig& cfg);
// Nhặt đồ ăn từ nguồn Food vào túi (mang theo).
InteractResult forage_at(ResourceNode& node, glm::vec2 actor_pos, Inventory& inv, const GameplayConfig& cfg);
// Dùng item trong túi.
InteractResult use_item(Inventory& inv, int slot, SurvivalState& s, const GameplayConfig& cfg);
// Nguồn gần nhất trong tầm (theo loại; nullopt = mọi loại).
ResourceNode* nearest_node(std::vector<ResourceNode>& nodes, glm::vec2 pos, float range,
                           std::optional<world::PoiKind> kind = std::nullopt);

// ============================================================================ chiến đấu
struct CombatState {
  float bite_cd = 0, dodge_cd = 0, iframes = 0, stagger = 0;
  std::vector<float> recent_hits;  // thời điểm bị cắn gần đây (để tính choáng)
  float notoriety = 0;             // tai tiếng khi gây hấn NPC (Phase 8 dùng)
  float since_last_combat = 1e9f;
  bool aggressive = false;         // bật "hung hăng" (cho phép PvP ở vùng restricted)
  int level = 1;
};

struct Combatant {
  glm::vec3 pos{0.0f};
  float yaw = 0;  // radian, 0 = +Z
  bool is_player = true;
  SurvivalState* survival = nullptr;
  CombatState* combat = nullptr;
};

enum class BiteResult { Hit, Miss, Dodged, OnCooldown, NoStamina, OutOfRange, OutOfArc, NotAllowed, Staggered, TargetDown };
const char* to_string(BiteResult r);

struct BiteOutcome {
  BiteResult result = BiteResult::Miss;
  float damage = 0;
  bool target_staggered = false;
  bool target_downed = false;
};

// Có cho phép PvP giữa hai NGƯỜI CHƠI tại vùng này không.
bool pvp_allowed(world::ZoneRule zone, const CombatState& attacker, const CombatState& target, const CombatConfig& c);
// Sát thương deterministic (không ngẫu nhiên).
float bite_damage(const CombatConfig& c, float attacker_strength_mod, float target_defense_mod);
// Xử lý yêu cầu cắn (phía server). now: giây mô phỏng.
BiteOutcome request_bite(Combatant& attacker, Combatant& target, world::ZoneRule zone, const GameplayConfig& cfg, float now);
bool request_dodge(Combatant& self, const GameplayConfig& cfg);
void tick_combat(CombatState& c, const CombatConfig& cfg, float dt);

// ============================================================================ hồi sinh
struct RespawnOutcome {
  glm::vec2 pos{0.0f};
  int dropped_items = 0;
};
// Gục quá thời gian -> hồi sinh ở nơi trú ẩn/spawn gần nhất, HP một phần, rơi tối đa 50% đồ TIÊU HAO.
// Không mất cosmetic, không mất nhân vật, không mất tiền thật.
RespawnOutcome respawn(SurvivalState& s, Inventory& inv, glm::vec2 died_at, const world::WorldData& w, const GameplayConfig& cfg);

// ============================================================================ HUD (data model, UI chỉ đọc)
struct HudModel {
  float hp = 0, hunger = 0, thirst = 0, stamina = 0, rest = 0;  // 0..1
  bool low_hunger = false, low_thirst = false, tired = false, downed = false, resting = false;
  float downed_left = 0;
  std::vector<ItemStack> slots;
  std::string zone;
  std::string prompt;  // gợi ý tương tác ("E: uống nước")
  std::string message; // thông báo gần nhất
};
HudModel make_hud(const SurvivalState& s, const Inventory& inv, const GameplayConfig& cfg, world::ZoneRule zone);

}  // namespace pb::sim
