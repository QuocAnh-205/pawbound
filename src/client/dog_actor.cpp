#include "client/dog_actor.hpp"

#include <cmath>
#include <format>

#include <glm/gtc/matrix_transform.hpp>

#include "core/log.hpp"
#include "geom/primitives.hpp"

namespace pb::client {

DogActor::DogActor(render::Renderer& r, ModelLibrary& lib, const sim::DogCatalog& cat, const std::string& breed_id,
                   glm::vec3 spawn, sim::DogAppearance look)
    : look_(look) {
  const sim::DogBreed* breed = cat.find(breed_id);
  if (!breed) {
    log::warn("dog", "không có breed '{}', dùng breed đầu tiên", breed_id);
    breed = &cat.breeds.front();
  }
  stats_ = sim::movement_for(cat, *breed);
  model_key_ = breed->model;
  binding_ = cat.models.at(breed->model);
  cur_.pos = prev_.pos = spawn;
  model_ = lib.get(binding_.asset_id);
  if (!model_) {
    log::warn("dog", "không có model '{}' — dùng chó PLACEHOLDER hình khối", binding_.asset_id);
    build_fallback(r);
  } else {
    gpu_ = model_->gpu;
    scale_ = binding_.scale * breed->size * look_.size_variant;
    gpu_.scale = scale_;
    if (!model_->cpu.skins.empty()) {
      skeleton_ = model_->cpu.skins[0];
      auto find = [&](const char* key) -> const asset::AnimationClip* {
        return model_->cpu.find_animation(binding_.clips.at(key));
      };
      clip_idle_ = find("idle");
      clip_walk_ = find("walk");
      clip_run_ = find("run");
      skinned_ = clip_idle_ && clip_walk_ && clip_run_;
      if (!skinned_) log::warn("dog", "model '{}' thiếu clip idle/walk/run — hiển thị tĩnh", binding_.asset_id);
    }
    placeholder_ = model_->quality != "production";
    for (auto& p : gpu_.parts) p.material.base_color *= glm::vec4(look_.coat_tint, 1.0f);
  }
  auto ring = geom::make_cylinder(stats_.radius, 0.02f, 24, true);
  debug_ring_ = r.upload_mesh(ring, std::format("dog/debug_ring/{:.3f}", stats_.radius));
}

void DogActor::build_fallback(render::Renderer& r) {
  placeholder_ = true;
  geom::MeshData m;
  const glm::vec4 fur(0.78f, 0.55f, 0.32f, 1.0f);
  auto T = [](glm::vec3 p, glm::vec3 s) { return glm::scale(glm::translate(glm::mat4(1.0f), p), s); };
  const auto cube = geom::make_box({1, 1, 1});
  m.append(cube, T({0, 0.45f, 0}, {0.3f, 0.26f, 0.7f}), fur);
  m.append(cube, T({0, 0.6f, 0.42f}, {0.22f, 0.22f, 0.26f}), fur);
  for (int k = 0; k < 4; ++k) m.append(cube, T({k % 2 ? 0.11f : -0.11f, 0.17f, k < 2 ? 0.27f : -0.27f}, {0.08f, 0.34f, 0.08f}), fur);
  GpuModelPart part;
  part.mesh = r.upload_mesh(m, "dog/fallback_boxes");
  part.material.roughness = 0.8f;
  gpu_.parts.push_back(part);
  gpu_.scale = 1.0f;
}

void DogActor::fixed_update(const InputState& in, const geom::Camera25D& cam, const sim::CollisionWorld& world,
                            float dt, float speed_mult, bool allow_sprint, bool frozen) {
  prev_ = cur_;
  sim::MotorInput mi;
  if (!frozen) {
    const glm::vec3 dir = cam.ground_right() * in.move.x + cam.ground_forward() * in.move.y;
    mi.move = {dir.x, dir.z};
    mi.sprint = in.sprint && allow_sprint;
    mi.walk = in.walk;
    mi.jump = in.jump;
  }
  sim::MovementStats st = stats_;
  st.walk_speed *= speed_mult;
  st.trot_speed *= speed_mult;
  st.run_speed *= speed_mult;
  st.sprint_speed *= speed_mult;
  sim::step_motor(cur_, mi, st, world, dt);
}

void DogActor::set_visual(glm::vec3 pos, float yaw, sim::Gait gait) {
  prev_ = cur_;
  cur_.pos = pos;
  cur_.yaw = yaw;
  cur_.gait = gait;
  cur_.grounded = gait != sim::Gait::Air;
  // Tốc độ đại diện của gait để AnimGraph chọn tốc độ phát
  static constexpr float kSpeed[] = {0.0f, 1.5f, 3.5f, 6.0f, 9.0f, 6.0f};
  const float v = kSpeed[std::min<size_t>(static_cast<size_t>(gait), 5)];
  cur_.vel = glm::vec2(std::sin(yaw), std::cos(yaw)) * v;
}

void DogActor::teleport(glm::vec3 pos) {
  cur_.pos = prev_.pos = pos;
  cur_.vel = prev_.vel = glm::vec2(0.0f);
  cur_.vy = 0;
  cur_.grounded = true;
}

glm::vec3 DogActor::position(float alpha) const { return glm::mix(prev_.pos, cur_.pos, alpha); }

void DogActor::update_visual(float dt) {
  anim_.update(cur_.gait, glm::length(cur_.vel), binding_, dt);
  if (!skinned_) return;
  auto clip = [&](const std::string& key) {
    return key == "idle" ? clip_idle_ : key == "walk" ? clip_walk_ : clip_run_;
  };
  asset::sample(*clip(anim_.current().clip), skeleton_, anim_.current().time, true, pose_a_);
  const float w = anim_.blend();
  if (w < 1.0f) {
    asset::sample(*clip(anim_.previous().clip), skeleton_, anim_.previous().time, true, pose_b_);
    asset::blend(pose_b_, pose_a_, w, pose_);
  } else {
    pose_ = pose_a_;
  }
  asset::skinning_matrices(skeleton_, pose_, joints_);
}

void DogActor::append_draws(float alpha, bool debug, std::vector<render::DrawItem>& out) {
  const glm::vec3 p = position(alpha);
  // Nội suy yaw theo đường ngắn nhất
  float dy = cur_.yaw - prev_.yaw;
  while (dy > 3.14159265f) dy -= 6.2831853f;
  while (dy < -3.14159265f) dy += 6.2831853f;
  const float yaw = prev_.yaw + dy * alpha + glm::radians(binding_.yaw_offset_deg);
  const glm::mat4 world = glm::rotate(glm::translate(glm::mat4(1.0f), p), yaw, glm::vec3(0, 1, 0));
  const size_t first = out.size();
  client::append_draws(gpu_, world, out);
  for (size_t i = first; i < out.size(); ++i) {
    out[i].material.cutaway = false;  // nhân vật không bao giờ bị làm mờ
    out[i].material.base_color *= tint_;
    if (skinned_ && gpu_.parts[i - first].primitive_skin >= 0) out[i].joints = &joints_;
  }
  if (debug) {
    render::Material m{{0.1f, 0.9f, 0.3f, 1.0f}, 0.0f, 0.5f};
    m.unlit = true;
    m.cutaway = false;
    out.push_back({debug_ring_, glm::translate(glm::mat4(1.0f), p + glm::vec3(0, 0.05f, 0)), m, false});
  }
}

std::string DogActor::debug_text() const {
  return std::format("pos ({:.1f}, {:.2f}, {:.1f}) v {:.2f} m/s gait {} {} anim {}x{:.2f}{}", cur_.pos.x, cur_.pos.y,
                     cur_.pos.z, glm::length(cur_.vel), sim::to_string(cur_.gait), cur_.grounded ? "đất" : "KHÔNG",
                     anim_.current().clip, anim_.current().rate, cur_.blocked ? " [va chạm]" : "");
}

}  // namespace pb::client
