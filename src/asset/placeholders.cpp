#include "asset/placeholders.hpp"

#include <cmath>
#include <numbers>

#include <glm/gtc/matrix_transform.hpp>

#include "geom/primitives.hpp"

namespace pb::asset {
namespace {
constexpr float kPi = std::numbers::pi_v<float>;

glm::vec4 quat_axis(glm::vec3 axis, float deg) {
  const glm::quat q = glm::angleAxis(glm::radians(deg), glm::normalize(axis));
  return {q.x, q.y, q.z, q.w};
}

Track rot_track(int joint, glm::vec3 axis, float duration, int keys, float amp_deg, float phase, float freq = 1.0f) {
  Track t;
  t.joint = joint;
  t.path = Path::Rotation;
  t.interp = Interp::Linear;
  for (int k = 0; k <= keys; ++k) {
    const float time = duration * static_cast<float>(k) / static_cast<float>(keys);
    const float a = amp_deg * std::sin(2.0f * kPi * freq * time / duration + phase);
    t.times.push_back(time);
    t.values.push_back(quat_axis(axis, a));
  }
  return t;
}

Track bob_track(int joint, glm::vec3 base, float duration, int keys, float amp, float freq) {
  Track t;
  t.joint = joint;
  t.path = Path::Translation;
  for (int k = 0; k <= keys; ++k) {
    const float time = duration * static_cast<float>(k) / static_cast<float>(keys);
    t.times.push_back(time);
    t.values.push_back(glm::vec4(base + glm::vec3(0, amp * std::sin(2.0f * kPi * freq * time / duration), 0), 0));
  }
  return t;
}
}  // namespace

void make_placeholder_dog(PlaceholderDog& out) {
  Skeleton& s = out.skeleton;
  s = {};
  auto add_joint = [&](const char* name, int parent, glm::vec3 t) {
    s.names.emplace_back(name);
    s.parents.push_back(parent);
    Transform tr;
    tr.t = t;
    s.bind_local.push_back(tr);
  };
  enum { kRoot, kBody, kHead, kTail, kFL, kFR, kBL, kBR };
  const glm::vec3 body_t(0, 0.45f, 0);
  add_joint("root", -1, {0, 0, 0});
  add_joint("body", kRoot, body_t);
  add_joint("head", kBody, {0, 0.12f, 0.36f});
  add_joint("tail", kBody, {0, 0.08f, -0.37f});
  add_joint("leg_fl", kBody, {-0.11f, -0.06f, 0.27f});
  add_joint("leg_fr", kBody, {0.11f, -0.06f, 0.27f});
  add_joint("leg_bl", kBody, {-0.11f, -0.06f, -0.27f});
  add_joint("leg_br", kBody, {0.11f, -0.06f, -0.27f});
  std::vector<glm::mat4> world(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    const glm::mat4 local = s.bind_local[i].matrix();
    world[i] = s.parents[i] >= 0 ? world[static_cast<size_t>(s.parents[i])] * local : local;
    s.inverse_bind.push_back(glm::inverse(world[i]));
  }

  GlbDesc& d = out.desc;
  d = {};
  d.name = "placeholder_dog";
  d.base_color = {0.78f, 0.55f, 0.32f, 1.0f};
  d.roughness = 0.8f;
  d.skeleton = &s;
  auto part = [&](int joint, glm::vec3 size, glm::vec3 offset_from_joint, glm::vec4 tint) {
    const glm::vec3 jp(world[static_cast<size_t>(joint)][3]);
    const auto box = geom::make_box(size);
    const size_t first = d.mesh.vertices.size();
    d.mesh.append(box, glm::translate(glm::mat4(1.0f), jp + offset_from_joint), tint);
    for (size_t i = first; i < d.mesh.vertices.size(); ++i) {
      d.joints.push_back({static_cast<uint16_t>(joint), 0, 0, 0});
      d.weights.emplace_back(1, 0, 0, 0);
    }
  };
  const glm::vec4 fur(1.0f), dark(0.6f, 0.5f, 0.45f, 1.0f);
  part(kBody, {0.30f, 0.26f, 0.72f}, {0, 0, 0}, fur);
  part(kHead, {0.22f, 0.22f, 0.26f}, {0, 0.04f, 0.06f}, fur);
  part(kHead, {0.12f, 0.10f, 0.14f}, {0, -0.02f, 0.24f}, dark);      // mõm
  part(kHead, {0.06f, 0.10f, 0.04f}, {-0.07f, 0.18f, 0.02f}, dark);  // tai
  part(kHead, {0.06f, 0.10f, 0.04f}, {0.07f, 0.18f, 0.02f}, dark);
  part(kTail, {0.05f, 0.05f, 0.26f}, {0, 0.05f, -0.12f}, fur);
  for (int leg : {kFL, kFR, kBL, kBR}) part(leg, {0.08f, 0.38f, 0.08f}, {0, -0.19f, 0}, fur);
  d.mesh.recompute_bounds();

  const glm::vec3 X(1, 0, 0), Y(0, 1, 0);
  AnimationClip idle{"idle", 2.0f, {}};
  idle.tracks.push_back(bob_track(kBody, body_t, 2.0f, 16, 0.008f, 1.0f));
  idle.tracks.push_back(rot_track(kTail, Y, 2.0f, 16, 25.0f, 0.0f, 4.0f));
  idle.tracks.push_back(rot_track(kHead, Y, 2.0f, 16, 6.0f, 0.0f, 1.0f));
  AnimationClip walk{"walk", 1.0f, {}};
  walk.tracks.push_back(bob_track(kBody, body_t, 1.0f, 16, 0.015f, 2.0f));
  walk.tracks.push_back(rot_track(kFL, X, 1.0f, 16, 25.0f, 0.0f));
  walk.tracks.push_back(rot_track(kBR, X, 1.0f, 16, 25.0f, 0.0f));
  walk.tracks.push_back(rot_track(kFR, X, 1.0f, 16, 25.0f, kPi));
  walk.tracks.push_back(rot_track(kBL, X, 1.0f, 16, 25.0f, kPi));
  walk.tracks.push_back(rot_track(kTail, Y, 1.0f, 16, 15.0f, 0.0f, 2.0f));
  AnimationClip run{"run", 0.5f, {}};
  run.tracks.push_back(bob_track(kBody, body_t, 0.5f, 16, 0.04f, 1.0f));
  run.tracks.push_back(rot_track(kBody, X, 0.5f, 16, 6.0f, 0.0f));
  run.tracks.push_back(rot_track(kFL, X, 0.5f, 16, 50.0f, 0.0f));
  run.tracks.push_back(rot_track(kFR, X, 0.5f, 16, 50.0f, 0.3f));
  run.tracks.push_back(rot_track(kBL, X, 0.5f, 16, 50.0f, kPi));
  run.tracks.push_back(rot_track(kBR, X, 0.5f, 16, 50.0f, kPi + 0.3f));
  d.animations = {idle, walk, run};
}

}  // namespace pb::asset
