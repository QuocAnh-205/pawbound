#pragma once
// Vòng gameplay single-player (Phase 7): áp đúng luật của pb_sim mà dedicated server sẽ chạy ở Phase 10.
// Client chỉ chuyển input -> yêu cầu; mọi kết quả do các hàm luật quyết định.
#include <memory>
#include <string>
#include <vector>

#include "client/dog_actor.hpp"
#include "client/model_library.hpp"
#include "client/npc_view.hpp"
#include "sim/nav.hpp"
#include "sim/npc.hpp"
#include "sim/world_sim.hpp"
#include "render/overlay.hpp"
#include "sim/gameplay.hpp"
#include "world/world_types.hpp"

namespace pb::client {

class GameSession {
 public:
  GameSession(render::Renderer& r, const sim::GameplayConfig& cfg, const sim::DogCatalog& cat,
              const world::WorldData& world, const sim::CollisionWorld& collision, const std::filesystem::path& asset_root,
              const std::string& breed, glm::vec3 spawn, const sim::NpcConfig& npc_cfg,
              const sim::WorldSimConfig& world_cfg);

  // pressed_keys: tên phím SDL vừa nhấn trong tick (1..6, G, Z...).
  void fixed_update(const InputState& in, const std::vector<std::string>& pressed_keys, const geom::Camera25D& cam,
                    float dt);
  void update_visual(float dt);
  void append_draws(float alpha, bool debug, std::vector<render::DrawItem>& out);
  void draw_hud(render::Overlay& o, int width, int height, float hour) const;

  [[nodiscard]] DogActor& player() { return *player_; }
  [[nodiscard]] const sim::SurvivalState& survival() const { return sv_; }
  [[nodiscard]] const sim::Inventory& inventory() const { return inv_; }
  [[nodiscard]] float sim_time() const { return time_; }
  [[nodiscard]] std::string summary() const;

  // ---- Hỗ trợ kịch bản QA tự động (--scenario): không dùng trong gameplay thường.
  bool qa_teleport_near(world::PoiKind kind);
  bool qa_face_npc(size_t i, float distance);
  void qa_set_needs(float hunger, float thirst, float rest);
  [[nodiscard]] size_t npc_count() const { return npcs_.size(); }
  [[nodiscard]] float npc_hp(size_t i) const { return npcs_[i].sv.hp; }
  [[nodiscard]] const std::string& last_message() const { return message_; }
  [[nodiscard]] const sim::NpcManager& npcs() const { return *npc_mgr_; }
  [[nodiscard]] sim::WorldSim& world_sim() { return *world_sim_; }
  [[nodiscard]] const sim::WorldSim& world_sim() const { return *world_sim_; }
  // Debug/QA (phím T/Y/U, tham số --weather/--event)
  void debug_next_weather();
  void debug_event(std::optional<sim::EventType> t);
  [[nodiscard]] float lightning() const { return lightning_; }

 private:
  struct Npc {
    std::unique_ptr<DogActor> actor;
    sim::SurvivalState sv;
    sim::CombatState cb;
    glm::vec3 home;
    float respawn_timer = 0;
  };
  void say(std::string msg);
  void interact(const std::string& key);

  const sim::GameplayConfig& cfg_;
  std::unique_ptr<ModelLibrary> library_;
  std::unique_ptr<sim::NavGraph> nav_;
  std::unique_ptr<sim::NpcManager> npc_mgr_;
  std::unique_ptr<NpcView> npc_view_;
  std::vector<sim::Noise> noises_;
  std::unique_ptr<sim::WorldSim> world_sim_;
  render::MeshHandle rain_mesh_, beacon_;
  float rain_phase_ = 0, lightning_ = 0, lightning_timer_ = 5;
  uint64_t fx_rng_ = 0x1234567;
  uint32_t ai_target_ = 0;  // id NPC AI đang nhắm (0 = không)
  const world::WorldData& world_;
  const sim::CollisionWorld& collision_;
  std::unique_ptr<DogActor> player_;
  sim::SurvivalState sv_;
  sim::CombatState cb_;
  sim::Inventory inv_;
  std::vector<sim::ResourceNode> nodes_;
  std::vector<glm::vec2> shelters_;
  std::vector<Npc> npcs_;
  bool resting_ = false, sprinting_ = false, sniffing_ = false;
  float time_ = 0;
  std::string message_;
  float message_t_ = 0;
  std::string prompt_;
  int target_ = -1;
  render::MeshHandle marker_;
};

}  // namespace pb::client
