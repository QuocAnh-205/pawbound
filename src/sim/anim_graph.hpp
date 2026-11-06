#pragma once
// State machine animation (logic thuần, test được): chọn clip theo gait + crossfade,
// tốc độ phát tỉ lệ với tốc độ di chuyển để chân không "trượt".
#include <string>

#include "sim/dog_archetype.hpp"
#include "sim/dog_motor.hpp"

namespace pb::sim {

struct AnimLayer {
  std::string clip;  // "idle" | "walk" | "run" (khóa logic, map sang tên clip thật qua ModelBinding)
  float time = 0.0f;
  float rate = 1.0f;
};

class AnimGraph {
 public:
  explicit AnimGraph(float crossfade_seconds = 0.2f) : fade_(crossfade_seconds) {}
  void update(Gait gait, float speed, const ModelBinding& mb, float dt);
  [[nodiscard]] const AnimLayer& current() const { return cur_; }
  [[nodiscard]] const AnimLayer& previous() const { return prev_; }
  // Trọng số của layer hiện tại (0..1); 1 = không còn blend.
  [[nodiscard]] float blend() const { return fade_ <= 0 ? 1.0f : std::min(1.0f, fade_t_ / fade_); }
  [[nodiscard]] static std::string clip_for(Gait g);

 private:
  AnimLayer cur_{"idle", 0.0f, 1.0f};
  AnimLayer prev_{"idle", 0.0f, 1.0f};
  float fade_;
  float fade_t_ = 1e9f;
};

}  // namespace pb::sim
