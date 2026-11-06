#include "asset/animation.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include <glm/gtc/matrix_transform.hpp>

namespace pb::asset {

glm::mat4 Transform::matrix() const {
  return glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s);
}

int Skeleton::find(const std::string& name) const {
  for (size_t i = 0; i < names.size(); ++i)
    if (names[i] == name) return static_cast<int>(i);
  return -1;
}

namespace {
glm::vec4 hermite(const glm::vec4& p0, const glm::vec4& m0, const glm::vec4& p1, const glm::vec4& m1, float t,
                  float dt) {
  const float t2 = t * t, t3 = t2 * t;
  return (2 * t3 - 3 * t2 + 1) * p0 + (t3 - 2 * t2 + t) * dt * m0 + (-2 * t3 + 3 * t2) * p1 + (t3 - t2) * dt * m1;
}

glm::vec4 sample_track(const Track& tr, float time) {
  const auto& ts = tr.times;
  const bool cubic = tr.interp == Interp::CubicSpline;
  auto value = [&](size_t k) { return cubic ? tr.values[k * 3 + 1] : tr.values[k]; };
  if (ts.empty()) return {};
  if (time <= ts.front()) return value(0);
  if (time >= ts.back()) return value(ts.size() - 1);
  const auto it = std::upper_bound(ts.begin(), ts.end(), time);
  const size_t k1 = static_cast<size_t>(it - ts.begin());
  const size_t k0 = k1 - 1;
  const float dt = ts[k1] - ts[k0];
  const float u = dt > 0 ? (time - ts[k0]) / dt : 0.0f;
  switch (tr.interp) {
    case Interp::Step: return value(k0);
    case Interp::CubicSpline:
      return hermite(tr.values[k0 * 3 + 1], tr.values[k0 * 3 + 2], tr.values[k1 * 3 + 1], tr.values[k1 * 3], u, dt);
    case Interp::Linear:
    default:
      if (tr.path == Path::Rotation) {
        const glm::vec4 a = value(k0), b = value(k1);
        const glm::quat qa(a.w, a.x, a.y, a.z), qb(b.w, b.x, b.y, b.z);
        const glm::quat q = glm::slerp(qa, glm::dot(qa, qb) < 0 ? -qb : qb, u);
        return {q.x, q.y, q.z, q.w};
      }
      return glm::mix(value(k0), value(k1), u);
  }
}
}  // namespace

void sample(const AnimationClip& clip, const Skeleton& skel, float time, bool loop, Pose& out) {
  out = skel.bind_local;
  if (clip.duration > 0.0f) {
    if (loop) {
      time = std::fmod(time, clip.duration);
      if (time < 0) time += clip.duration;
    } else {
      time = std::clamp(time, 0.0f, clip.duration);
    }
  }
  for (const auto& tr : clip.tracks) {
    if (tr.joint < 0 || static_cast<size_t>(tr.joint) >= out.size()) continue;
    const glm::vec4 v = sample_track(tr, time);
    auto& j = out[static_cast<size_t>(tr.joint)];
    switch (tr.path) {
      case Path::Translation: j.t = glm::vec3(v); break;
      case Path::Scale: j.s = glm::vec3(v); break;
      case Path::Rotation: j.r = glm::normalize(glm::quat(v.w, v.x, v.y, v.z)); break;
    }
  }
}

void blend(const Pose& a, const Pose& b, float w, Pose& out) {
  const size_t n = std::min(a.size(), b.size());
  out.resize(n);
  for (size_t i = 0; i < n; ++i) {
    out[i].t = glm::mix(a[i].t, b[i].t, w);
    out[i].s = glm::mix(a[i].s, b[i].s, w);
    const glm::quat qb = glm::dot(a[i].r, b[i].r) < 0 ? -b[i].r : b[i].r;
    out[i].r = glm::normalize(glm::slerp(a[i].r, qb, w));
  }
}

void model_matrices(const Skeleton& skel, const Pose& pose, std::vector<glm::mat4>& out) {
  out.resize(skel.size());
  for (size_t i = 0; i < skel.size(); ++i) {
    const glm::mat4 local = (i < pose.size() ? pose[i] : skel.bind_local[i]).matrix();
    const int p = skel.parents[i];
    out[i] = (p >= 0 ? out[static_cast<size_t>(p)] : skel.root_transform) * local;
  }
}

void skinning_matrices(const Skeleton& skel, const Pose& pose, std::vector<glm::mat4>& out) {
  model_matrices(skel, pose, out);
  for (size_t i = 0; i < out.size(); ++i) out[i] = out[i] * skel.inverse_bind[i];
}

std::string validate_skeleton(const Skeleton& s) {
  if (s.parents.empty()) return "skeleton không có joint";
  if (s.inverse_bind.size() != s.parents.size()) return "số inverse bind matrix không khớp số joint";
  if (s.bind_local.size() != s.parents.size() || s.names.size() != s.parents.size())
    return "kích thước dữ liệu joint không khớp";
  for (size_t i = 0; i < s.parents.size(); ++i)
    if (s.parents[i] >= static_cast<int>(i))
      return std::format("joint {} ('{}') có parent {} không đứng trước", i, s.names[i], s.parents[i]);
  return {};
}

}  // namespace pb::asset
