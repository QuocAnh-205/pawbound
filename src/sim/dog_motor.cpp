#include "sim/dog_motor.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace pb::sim {

const char* to_string(Gait g) {
  switch (g) {
    case Gait::Idle: return "idle";
    case Gait::Walk: return "walk";
    case Gait::Trot: return "trot";
    case Gait::Run: return "run";
    case Gait::Sprint: return "sprint";
    case Gait::Air: return "air";
  }
  return "?";
}

float target_speed(const MotorInput& in, const MovementStats& s) {
  const float mag = std::min(glm::length(in.move), 1.0f);
  if (mag < 0.1f) return 0.0f;
  if (in.walk) return s.walk_speed * mag;
  if (in.sprint) return s.sprint_speed * mag;
  // Cần analog nhẹ -> đi bộ/chạy nước kiệu; bàn phím (mag=1) -> chạy.
  if (mag < 0.5f) return s.walk_speed;
  if (mag < 0.85f) return s.trot_speed;
  return s.run_speed;
}

Gait gait_for_speed(float v, const MovementStats& s) {
  if (v < 0.15f) return Gait::Idle;
  if (v < (s.walk_speed + s.trot_speed) * 0.5f) return Gait::Walk;
  if (v < (s.trot_speed + s.run_speed) * 0.5f) return Gait::Trot;
  if (v < (s.run_speed + s.sprint_speed) * 0.5f) return Gait::Run;
  return Gait::Sprint;
}

void step_motor(MotorState& st, const MotorInput& in, const MovementStats& s, const CollisionWorld& world, float dt) {
  // --- vận tốc ngang: tiến dần về vận tốc mục tiêu (giới hạn gia tốc)
  glm::vec2 dir = in.move;
  const float len = glm::length(dir);
  if (len > 1.0f) dir /= len;
  const float speed = target_speed(in, s);
  const glm::vec2 target = len > 1e-4f ? glm::normalize(dir) * speed : glm::vec2(0.0f);
  const float control = st.grounded ? 1.0f : 0.35f;  // ít điều khiển khi trên không
  const glm::vec2 dv = target - st.vel;
  const float rate = (glm::dot(target, target) > glm::dot(st.vel, st.vel) ? s.accel : s.decel) * control;
  const float max_dv = rate * dt;
  const float dvl = glm::length(dv);
  st.vel += dvl > max_dv ? dv * (max_dv / dvl) : dv;

  // --- hướng nhìn quay dần về hướng di chuyển
  if (glm::length(st.vel) > 0.2f) {
    const float want = std::atan2(st.vel.x, st.vel.y);
    float diff = want - st.yaw;
    while (diff > std::numbers::pi_v<float>) diff -= 2 * std::numbers::pi_v<float>;
    while (diff < -std::numbers::pi_v<float>) diff += 2 * std::numbers::pi_v<float>;
    const float max_turn = glm::radians(s.turn_rate_deg) * dt;
    st.yaw += std::clamp(diff, -max_turn, max_turn);
  }

  // --- dốc: chặn đi lên dốc quá đứng, chậm lại khi lên dốc
  glm::vec2 step = st.vel * dt;
  if (st.grounded && glm::length(step) > 1e-5f) {
    const glm::vec2 p0(st.pos.x, st.pos.z);
    const glm::vec2 probe = p0 + glm::normalize(step) * 0.5f;
    const float slope = (world.ground_height(probe.x, probe.y) - world.ground_height(p0.x, p0.y)) / 0.5f;
    if (slope > std::tan(glm::radians(s.max_slope_deg))) step = glm::vec2(0.0f);
    else if (slope > 0) step *= 1.0f - std::min(0.5f, slope * 0.5f);
  }

  // --- di chuyển + va chạm (chia nhỏ để không xuyên vật mỏng khi chạy nhanh)
  glm::vec2 p(st.pos.x, st.pos.z);
  const int sub = std::max(1, static_cast<int>(std::ceil(glm::length(step) / (s.radius * 0.5f))));
  st.blocked = false;
  for (int i = 0; i < sub; ++i) {
    p += step / static_cast<float>(sub);
    const auto hit = world.resolve(p, s.radius, st.pos.y, s.step_height);
    if (hit.hit) {
      st.blocked = true;
      // bỏ thành phần vận tốc lao vào vật cản -> trượt dọc tường
      const float into = glm::dot(st.vel, hit.normal);
      if (into < 0) st.vel -= hit.normal * into;
    }
  }

  // --- trục đứng: nhảy, trọng lực, bám đất
  const float ground = world.ground_height(p.x, p.y);
  if (st.grounded && in.jump && s.jump_speed > 0) {
    st.vy = s.jump_speed;
    st.grounded = false;
  }
  if (!st.grounded) {
    st.vy -= s.gravity * dt;
    st.pos.y += st.vy * dt;
    if (st.pos.y <= ground) {
      st.pos.y = ground;
      st.vy = 0;
      st.grounded = true;
    }
  } else if (ground < st.pos.y - 0.6f) {
    st.grounded = false;  // rơi khỏi gờ cao
    st.vy = 0;
  } else {
    st.pos.y = ground;  // bám theo địa hình
  }
  st.pos.x = p.x;
  st.pos.z = p.y;
  st.gait = st.grounded ? gait_for_speed(glm::length(st.vel), s) : Gait::Air;
}

}  // namespace pb::sim
