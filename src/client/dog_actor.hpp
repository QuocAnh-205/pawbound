#pragma once
// Chú chó người chơi phía client: mô phỏng (sim::DogMotor, fixed timestep) + hiển thị (model skinned + AnimGraph).
// Toàn bộ setup đến từ data (breeds.json + assets/manifest.json); không có thao tác editor.
#include <filesystem>
#include <string>
#include <vector>

#include "asset/model.hpp"
#include "client/model_library.hpp"
#include "client/model_upload.hpp"
#include "core/services.hpp"
#include "geom/camera.hpp"
#include "render/renderer.hpp"
#include "sim/anim_graph.hpp"
#include "sim/collision.hpp"
#include "sim/dog_archetype.hpp"
#include "sim/dog_motor.hpp"

namespace pb::client {

class DogActor {
 public:
  DogActor(render::Renderer& r, ModelLibrary& lib, const sim::DogCatalog& cat, const std::string& breed_id,
           glm::vec3 spawn, sim::DogAppearance look = {});

  // Một tick mô phỏng; input theo camera (WASD tương đối hướng nhìn).
  // speed_mult: hệ số từ sinh tồn (mệt); allow_sprint: đủ thể lực; frozen: đang gục/nghỉ (không di chuyển).
  void fixed_update(const InputState& in, const geom::GameCamera& cam, const sim::CollisionWorld& world, float dt,
                    float speed_mult = 1.0f, bool allow_sprint = true, bool frozen = false);
  void teleport(glm::vec3 pos);
  // Chế độ online: hiển thị theo trạng thái server (không chạy motor cục bộ).
  void set_visual(glm::vec3 pos, float yaw, sim::Gait gait);
  void set_yaw(float yaw) { cur_.yaw = prev_.yaw = yaw; }
  void set_tint(const glm::vec4& tint) { tint_ = tint; }
  // Animation (theo render frame).
  void update_visual(float dt);
  void append_draws(float alpha, bool debug, std::vector<render::DrawItem>& out);

  [[nodiscard]] glm::vec3 position(float alpha) const;
  [[nodiscard]] const sim::MotorState& state() const { return cur_; }
  [[nodiscard]] std::string debug_text() const;
  [[nodiscard]] bool placeholder() const { return placeholder_; }
  [[nodiscard]] const std::string& model_id() const { return model_key_; }

 private:
  void build_fallback(render::Renderer& r);

  sim::MovementStats stats_;
  sim::ModelBinding binding_;
  std::string model_key_;
  sim::DogAppearance look_;
  sim::MotorState prev_, cur_;
  sim::AnimGraph anim_;
  asset::Skeleton skeleton_;
  const LoadedModel* model_ = nullptr;
  float scale_ = 1.0f;
  const asset::AnimationClip* clip_idle_ = nullptr;
  const asset::AnimationClip* clip_walk_ = nullptr;
  const asset::AnimationClip* clip_run_ = nullptr;
  asset::Pose pose_a_, pose_b_, pose_;
  std::vector<glm::mat4> joints_;
  GpuModel gpu_;
  bool skinned_ = false;
  bool placeholder_ = false;
  render::MeshHandle debug_ring_;
  glm::vec4 tint_{1.0f};
};

}  // namespace pb::client
