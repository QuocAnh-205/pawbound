#pragma once
// Hiển thị NPC từ NpcManager (dữ liệu sim). Người: hình khối PLACEHOLDER tô màu theo tính cách.
// Chó NPC: model chó placeholder skinned, AnimGraph riêng mỗi con. Nội suy vị trí giữa hai tick sim.
#include <unordered_map>
#include <vector>

#include "client/model_library.hpp"
#include "render/renderer.hpp"
#include "sim/anim_graph.hpp"
#include "sim/dog_archetype.hpp"
#include "sim/npc.hpp"

namespace pb::client {

class NpcView {
 public:
  NpcView(render::Renderer& r, ModelLibrary& lib, const sim::DogCatalog& cat);
  // Gọi sau mỗi tick sim để lưu vị trí trước/sau phục vụ nội suy.
  void after_tick(const std::vector<sim::Npc>& npcs);
  void update_visual(const std::vector<sim::Npc>& npcs, float dt);
  void append(const std::vector<sim::Npc>& npcs, float alpha, bool debug, std::vector<render::DrawItem>& out);

 private:
  struct Visual {
    glm::vec3 prev{0.0f}, cur{0.0f};
    float prev_yaw = 0, cur_yaw = 0;
    sim::AnimGraph anim;
    asset::Pose a, b, pose;
    std::vector<glm::mat4> joints;
    float bob = 0;
    bool seen = false;
  };
  std::unordered_map<uint32_t, Visual> vis_;
  render::MeshHandle human_mesh_;
  render::MeshHandle state_marker_;
  const LoadedModel* dog_ = nullptr;
  sim::ModelBinding dog_binding_;
};

}  // namespace pb::client
