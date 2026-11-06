#pragma once
// Camera 2.5D (ADR-002): orthographic, pitch cố định, yaw xoay theo bước 90° có nội suy, zoom giới hạn.
// Ma trận theo quy ước Vulkan: depth [0,1], trục Y clip-space hướng xuống (đã lật trong proj).
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace pb::geom {

struct Camera25DSettings {
  float pitch_deg = 50.0f;       // góc nhìn xuống so với mặt đất (35..60)
  float distance = 60.0f;        // khoảng cách từ target tới camera (chỉ ảnh hưởng near/far, ortho)
  float zoom = 8.0f;             // nửa chiều cao vùng nhìn (m)
  float zoom_min = 3.5f;
  float zoom_max = 30.0f;
  float yaw_lerp_speed = 10.0f;  // 1/giây
  float follow_speed = 8.0f;     // 1/giây
};

class Camera25D {
 public:
  explicit Camera25D(Camera25DSettings s = {}) : s_(s) { zoom_ = s.zoom; }

  // Xoay 90° (dir = +1 / -1). Nhận lệnh mới ngay cả khi đang xoay.
  void rotate_step(int dir) { target_yaw_step_ = (target_yaw_step_ + dir) & 3; yaw_goal_ += static_cast<float>(dir) * 90.0f; }
  void add_zoom(float delta) { zoom_ = std::clamp(zoom_ + delta, s_.zoom_min, s_.zoom_max); }
  void set_target(const glm::vec3& t, bool snap = false) {
    goal_ = t;
    if (snap) target_ = t;
  }
  // Làm mượt follow và yaw. dt: giây.
  void update(float dt) {
    const float kf = 1.0f - std::exp(-s_.follow_speed * dt);
    target_ += (goal_ - target_) * kf;
    const float ky = 1.0f - std::exp(-s_.yaw_lerp_speed * dt);
    yaw_ += (yaw_goal_ - yaw_) * ky;
    if (std::abs(yaw_goal_ - yaw_) < 0.01f) yaw_ = yaw_goal_;
  }

  [[nodiscard]] float yaw_deg() const { return yaw_; }
  [[nodiscard]] int yaw_step() const { return target_yaw_step_; }
  [[nodiscard]] float zoom() const { return zoom_; }
  [[nodiscard]] const glm::vec3& target() const { return target_; }

  // Hướng từ target tới camera (đơn vị).
  [[nodiscard]] glm::vec3 back_dir() const {
    const float p = glm::radians(s_.pitch_deg), y = glm::radians(yaw_);
    return glm::normalize(glm::vec3(std::sin(y) * std::cos(p), std::sin(p), std::cos(y) * std::cos(p)));
  }
  [[nodiscard]] glm::vec3 position() const { return target_ + back_dir() * s_.distance; }
  // Hướng "tiến" và "phải" trên mặt đất theo camera — dùng để chuyển input WASD.
  [[nodiscard]] glm::vec3 ground_forward() const {
    const float y = glm::radians(yaw_);
    return glm::vec3(-std::sin(y), 0.0f, -std::cos(y));
  }
  [[nodiscard]] glm::vec3 ground_right() const {
    const float y = glm::radians(yaw_);
    return glm::vec3(std::cos(y), 0.0f, -std::sin(y));
  }

  [[nodiscard]] glm::mat4 view() const { return glm::lookAt(position(), target_, glm::vec3(0, 1, 0)); }
  [[nodiscard]] glm::mat4 proj(float aspect) const {
    const float hh = zoom_, hw = zoom_ * aspect;
    glm::mat4 p = glm::orthoRH_ZO(-hw, hw, -hh, hh, 0.1f, s_.distance * 2.5f);
    p[1][1] *= -1.0f;  // Vulkan: Y xuống
    return p;
  }

 private:
  Camera25DSettings s_;
  glm::vec3 target_{0.0f}, goal_{0.0f};
  float yaw_ = 45.0f, yaw_goal_ = 45.0f;  // 45°: nhìn chéo kiểu isometric
  int target_yaw_step_ = 0;
  float zoom_;
};

// 6 mặt phẳng frustum (ax+by+cz+d ≥ 0 là bên trong), trích từ ma trận view-proj (Vulkan depth 0..1).
struct Frustum {
  std::array<glm::vec4, 6> planes{};
  static Frustum from_matrix(const glm::mat4& vp) {
    Frustum f;
    const glm::vec4 r0(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
    const glm::vec4 r1(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
    const glm::vec4 r2(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
    const glm::vec4 r3(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);
    f.planes = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r2, r3 - r2};
    for (auto& p : f.planes) p /= glm::length(glm::vec3(p));
    return f;
  }
  // true nếu AABB có thể nhìn thấy (có thể false-positive ở góc, không bao giờ false-negative).
  [[nodiscard]] bool intersects(const glm::vec3& mn, const glm::vec3& mx) const {
    for (const auto& p : planes) {
      const glm::vec3 v(p.x >= 0 ? mx.x : mn.x, p.y >= 0 ? mx.y : mn.y, p.z >= 0 ? mx.z : mn.z);
      if (p.x * v.x + p.y * v.y + p.z * v.z + p.w < 0) return false;
    }
    return true;
  }
};

}  // namespace pb::geom
