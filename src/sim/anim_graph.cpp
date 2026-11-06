#include "sim/anim_graph.hpp"

#include <algorithm>

namespace pb::sim {

std::string AnimGraph::clip_for(Gait g) {
  switch (g) {
    case Gait::Idle: return "idle";
    case Gait::Walk:
    case Gait::Trot: return "walk";
    case Gait::Run:
    case Gait::Sprint:
    case Gait::Air: return "run";  // chưa có clip nhảy riêng: giữ dáng chạy khi trên không
  }
  return "idle";
}

void AnimGraph::update(Gait gait, float speed, const ModelBinding& mb, float dt) {
  const std::string want = clip_for(gait);
  if (want != cur_.clip) {
    prev_ = cur_;
    cur_ = {want, 0.0f, 1.0f};
    fade_t_ = 0.0f;
  }
  if (cur_.clip == "walk") cur_.rate = std::clamp(speed / mb.walk_clip_speed, 0.5f, 2.2f);
  else if (cur_.clip == "run") cur_.rate = gait == Gait::Air ? 0.4f : std::clamp(speed / mb.run_clip_speed, 0.6f, 1.8f);
  else cur_.rate = 1.0f;
  cur_.time += dt * cur_.rate;
  prev_.time += dt * prev_.rate;
  fade_t_ += dt;
}

}  // namespace pb::sim
