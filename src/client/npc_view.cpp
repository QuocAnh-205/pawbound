#include "client/npc_view.hpp"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "geom/primitives.hpp"

namespace pb::client {
namespace {
glm::vec4 personality_color(sim::Personality p) {
  switch (p) {
    case sim::Personality::Friendly: return {0.35f, 0.75f, 0.35f, 1};
    case sim::Personality::Fearful: return {0.55f, 0.7f, 0.95f, 1};
    case sim::Personality::Hostile: return {0.85f, 0.25f, 0.2f, 1};
    case sim::Personality::Vendor: return {0.95f, 0.8f, 0.2f, 1};
    default: return {0.6f, 0.6f, 0.62f, 1};
  }
}
float lerp_angle(float a, float b, float t) {
  float d = b - a;
  while (d > 3.14159265f) d -= 6.2831853f;
  while (d < -3.14159265f) d += 6.2831853f;
  return a + d * t;
}
}  // namespace

NpcView::NpcView(render::Renderer& r, ModelLibrary& lib, const sim::DogCatalog& cat) {
  // Người placeholder (cao ~1,75 m): áo theo màu tính cách (base_color), quần/da/tóc cố định qua vertex color.
  geom::MeshData m;
  auto T = [](glm::vec3 p, glm::vec3 s) { return glm::scale(glm::translate(glm::mat4(1.0f), p), s); };
  const auto cube = geom::make_box({1, 1, 1});
  const glm::vec4 shirt(1, 1, 1, 1), pants(0.25f, 0.27f, 0.35f, 1), skin(0.93f, 0.78f, 0.65f, 1), hair(0.2f, 0.15f, 0.1f, 1);
  m.append(cube, T({-0.11f, 0.42f, 0}, {0.16f, 0.84f, 0.2f}), pants);
  m.append(cube, T({0.11f, 0.42f, 0}, {0.16f, 0.84f, 0.2f}), pants);
  m.append(cube, T({0, 1.15f, 0}, {0.46f, 0.62f, 0.26f}), shirt);
  m.append(cube, T({-0.29f, 1.12f, 0}, {0.11f, 0.58f, 0.13f}), shirt);
  m.append(cube, T({0.29f, 1.12f, 0}, {0.11f, 0.58f, 0.13f}), shirt);
  m.append(geom::make_sphere(0.14f, 8, 10), T({0, 1.6f, 0.01f}, glm::vec3(1)), skin);
  m.append(geom::make_sphere(0.15f, 8, 10), T({0, 1.66f, -0.02f}, {1, 0.6f, 1}), hair);
  human_mesh_ = r.upload_mesh(m, "npc/human_placeholder");
  state_marker_ = r.upload_mesh(geom::make_sphere(0.1f, 6, 8), "npc/state_marker");
  if (auto it = cat.models.find("dog_placeholder"); it != cat.models.end()) {
    dog_binding_ = it->second;
    dog_ = lib.get(dog_binding_.asset_id);
  }
}

void NpcView::after_tick(const std::vector<sim::Npc>& npcs) {
  for (const auto& n : npcs) {
    auto& v = vis_[n.id];
    if (!v.seen) {
      v.prev = v.cur = n.motor.pos;
      v.prev_yaw = v.cur_yaw = n.motor.yaw;
      v.seen = true;
      continue;
    }
    v.prev = v.cur;
    v.prev_yaw = v.cur_yaw;
    v.cur = n.motor.pos;
    v.cur_yaw = n.motor.yaw;
  }
  // Dọn visual của NPC đã despawn
  if (vis_.size() > npcs.size() * 2 + 32) {
    std::unordered_map<uint32_t, Visual> keep;
    for (const auto& n : npcs)
      if (auto it = vis_.find(n.id); it != vis_.end()) keep.emplace(n.id, std::move(it->second));
    vis_.swap(keep);
  }
}

void NpcView::update_visual(const std::vector<sim::Npc>& npcs, float dt) {
  for (const auto& n : npcs) {
    auto it = vis_.find(n.id);
    if (it == vis_.end()) continue;
    auto& v = it->second;
    const float speed = glm::length(n.motor.vel);
    v.bob += dt * speed * 6.0f;
    if (n.kind != sim::NpcKind::Dog || !dog_ || dog_->cpu.skins.empty()) continue;
    v.anim.update(n.sv.life == sim::LifeState::Alive ? n.motor.gait : sim::Gait::Idle, speed, dog_binding_, dt);
    const auto& sk = dog_->cpu.skins[0];
    auto clip = [&](const std::string& k) { return dog_->cpu.find_animation(dog_binding_.clips.at(k)); };
    const auto* c = clip(v.anim.current().clip);
    if (!c) continue;
    asset::sample(*c, sk, v.anim.current().time, true, v.a);
    if (v.anim.blend() < 1.0f) {
      if (const auto* pc = clip(v.anim.previous().clip)) {
        asset::sample(*pc, sk, v.anim.previous().time, true, v.b);
        asset::blend(v.b, v.a, v.anim.blend(), v.pose);
      } else {
        v.pose = v.a;
      }
    } else {
      v.pose = v.a;
    }
    asset::skinning_matrices(sk, v.pose, v.joints);
  }
}

void NpcView::append(const std::vector<sim::Npc>& npcs, float alpha, bool debug, std::vector<render::DrawItem>& out) {
  for (const auto& n : npcs) {
    auto it = vis_.find(n.id);
    if (it == vis_.end()) continue;
    auto& v = it->second;
    const glm::vec3 p = glm::mix(v.prev, v.cur, alpha);
    const float yaw = lerp_angle(v.prev_yaw, v.cur_yaw, alpha);
    const bool down = n.sv.life != sim::LifeState::Alive;
    glm::mat4 w = glm::rotate(glm::translate(glm::mat4(1.0f), p), yaw, glm::vec3(0, 1, 0));
    if (down) w = glm::rotate(w, 1.4f, glm::vec3(0, 0, 1));  // nằm gục
    if (n.kind == sim::NpcKind::Human) {
      render::Material m;
      m.base_color = personality_color(n.pers);
      m.roughness = 0.85f;
      m.cutaway = false;
      const float bob = std::abs(std::sin(v.bob)) * 0.04f;
      out.push_back({human_mesh_, glm::translate(w, glm::vec3(0, bob, 0)), m, true});
    } else if (dog_) {
      const glm::mat4 base = w * glm::scale(glm::mat4(1.0f), glm::vec3(dog_binding_.scale));
      const glm::vec4 tint = n.pers == sim::Personality::Aggressive ? glm::vec4(0.45f, 0.35f, 0.3f, 1)
                             : n.pers == sim::Personality::Friendly ? glm::vec4(1.0f, 0.92f, 0.75f, 1)
                                                                    : glm::vec4(0.75f, 0.7f, 0.65f, 1);
      for (const auto& part : dog_->gpu.parts) {
        render::DrawItem d{part.mesh, base * part.node_transform, part.material, true};
        d.material.base_color *= tint;
        d.material.cutaway = false;
        if (part.primitive_skin >= 0 && !v.joints.empty()) d.joints = &v.joints;
        out.push_back(d);
      }
    }
    if (debug) {  // chấm màu trạng thái trên đầu: đỏ = tấn công/dọa, xanh lá = thân thiện/theo, vàng = chạy trốn
      glm::vec4 c(0.8f, 0.8f, 0.8f, 1);
      switch (n.state) {
        case sim::NpcState::Fight:
        case sim::NpcState::Growl:
        case sim::NpcState::Scare:
        case sim::NpcState::Defend: c = {1, 0.1f, 0.1f, 1}; break;
        case sim::NpcState::Follow:
        case sim::NpcState::Approach:
        case sim::NpcState::Feed: c = {0.1f, 1, 0.2f, 1}; break;
        case sim::NpcState::Flee: c = {1, 0.9f, 0.1f, 1}; break;
        case sim::NpcState::SeekFood:
        case sim::NpcState::SeekWater:
        case sim::NpcState::Eat:
        case sim::NpcState::Drink: c = {0.2f, 0.6f, 1, 1}; break;
        default: break;
      }
      render::Material mk{c};
      mk.unlit = true;
      mk.cutaway = false;
      out.push_back({state_marker_, glm::translate(glm::mat4(1.0f), p + glm::vec3(0, n.kind == sim::NpcKind::Human ? 2.1f : 1.0f, 0)), mk, false});
    }
  }
}

}  // namespace pb::client
