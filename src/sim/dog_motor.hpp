#pragma once
// Bộ điều khiển chuyển động của chó (fixed timestep). Dùng chung client (prediction) và server (authoritative).
#include <cstdint>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "sim/collision.hpp"
#include "sim/dog_archetype.hpp"

namespace pb::sim {

enum class Gait : uint8_t { Idle, Walk, Trot, Run, Sprint, Air };
const char* to_string(Gait g);

struct MotorInput {
  glm::vec2 move{0.0f};  // hướng mong muốn trong không gian thế giới (x, z), độ dài ≤ 1
  bool sprint = false;
  bool walk = false;     // đi chậm (Ctrl)
  bool jump = false;     // cạnh lên
};

struct MotorState {
  glm::vec3 pos{0.0f};
  glm::vec2 vel{0.0f};   // vận tốc ngang (m/s)
  float vy = 0.0f;
  float yaw = 0.0f;      // radian; 0 = nhìn +Z
  bool grounded = true;
  Gait gait = Gait::Idle;
  bool blocked = false;  // va chạm trong tick vừa rồi
};

// Tốc độ mục tiêu theo input (trước tăng tốc/va chạm).
float target_speed(const MotorInput& in, const MovementStats& s);
Gait gait_for_speed(float speed, const MovementStats& s);

void step_motor(MotorState& st, const MotorInput& in, const MovementStats& s, const CollisionWorld& world, float dt);

}  // namespace pb::sim
