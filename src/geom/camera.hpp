#pragma once
// Camera trò chơi. Ba chế độ:
//  - ThirdPerson (mặc định): phối cảnh 3D, camera sau lưng nhân vật, xoay tự do bằng chuột/vuốt, cuộn để đổi khoảng cách.
//  - FirstPerson: phối cảnh 3D từ mắt con chó.
//  - Isometric: chế độ 2.5D cũ (orthographic, pitch cố định, xoay 90°) — giữ cho test/bản đồ.
// Ma trận theo quy ước Vulkan: depth [0,1], trục Y clip-space hướng xuống (đã lật trong proj).
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <numbers>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace pb::geom {

enum class CameraMode { ThirdPerson, FirstPerson, Isometric };

struct CameraSettings {
  // Isometric (2.5D)
  float iso_pitch_deg = 50.0f;
  float iso_distance = 60.0f;
  float zoom = 8.0f;  // nửa chiều cao vùng nhìn (m)
  float zoom_min = 3.5f, zoom_max = 30.0f;
  // Góc thứ ba
  float tp_distance = 3.0f;
  float tp_distance_min = 1.8f, tp_distance_max = 14.0f;
  float tp_pivot_height = 0.6f;  // điểm nhìn: trên lưng con chó
  float tp_fov_deg = 60.0f;
  // Góc thứ nhất
  float fp_eye_height = 0.55f;  // mắt chó (m)
  float fp_eye_forward = 0.35f;
  float fp_fov_deg = 75.0f;
  // Chung 3D
  float pitch_min_deg = -35.0f, pitch_max_deg = 75.0f;  // dương = nhìn xuống
  float far_plane = 420.0f;
  float yaw_lerp_speed = 10.0f;  // 1/giây (xoay bằng phím)
  float follow_speed = 12.0f;    // 1/giây
};

class GameCamera {
 public:
  explicit GameCamera(CameraMode mode = CameraMode::ThirdPerson, CameraSettings s = {}) : s_(s), mode_(mode) {
    zoom_ = s.zoom;
    dist_ = s.tp_distance;
    if (mode == CameraMode::Isometric) yaw_ = yaw_goal_ = 45.0f;
  }

  [[nodiscard]] CameraMode mode() const { return mode_; }
  [[nodiscard]] bool perspective() const { return mode_ != CameraMode::Isometric; }
  void set_mode(CameraMode m) {
    mode_ = m;
    if (m == CameraMode::Isometric) {
      yaw_goal_ = std::round(yaw_goal_ / 90.0f) * 90.0f + 45.0f;
    }
  }
  // V: chuyển góc thứ ba <-> góc thứ nhất
  void toggle_person() { set_mode(mode_ == CameraMode::FirstPerson ? CameraMode::ThirdPerson : CameraMode::FirstPerson); }

  // Xoay bằng phím (Q/R): 2.5D 90°/bước, 3D 45°/bước — mượt.
  void rotate_step(int dir) {
    target_yaw_step_ = (target_yaw_step_ + dir) & 3;
    yaw_goal_ += static_cast<float>(dir) * (mode_ == CameraMode::Isometric ? 90.0f : 45.0f);
  }
  // Nhìn bằng chuột/vuốt (độ). Chỉ có tác dụng ở chế độ 3D. dpitch dương = nhìn xuống.
  void look(float dyaw_deg, float dpitch_deg) {
    if (mode_ == CameraMode::Isometric) return;
    yaw_ += dyaw_deg;
    yaw_goal_ = yaw_;
    pitch_ = std::clamp(pitch_ + dpitch_deg, s_.pitch_min_deg, s_.pitch_max_deg);
  }
  void add_zoom(float delta) {
    if (mode_ == CameraMode::Isometric) zoom_ = std::clamp(zoom_ + delta, s_.zoom_min, s_.zoom_max);
    else if (mode_ == CameraMode::ThirdPerson) dist_ = std::clamp(dist_ + delta * 0.6f, s_.tp_distance_min, s_.tp_distance_max);
  }
  void set_target(const glm::vec3& t, bool snap = false) {
    goal_ = t;
    if (snap) target_ = t;
  }
  // Độ cao mặt đất — camera góc thứ ba không chui xuống đất.
  void set_ground(std::function<float(float, float)> f) { ground_ = std::move(f); }

  void update(float dt) {
    const float fs = mode_ == CameraMode::FirstPerson ? 40.0f : s_.follow_speed;  // góc nhất: bám sát, không trễ
    const float kf = 1.0f - std::exp(-fs * dt);
    target_ += (goal_ - target_) * kf;
    const float ky = 1.0f - std::exp(-s_.yaw_lerp_speed * dt);
    yaw_ += (yaw_goal_ - yaw_) * ky;
    if (std::abs(yaw_goal_ - yaw_) < 0.01f) yaw_ = yaw_goal_;
  }

  [[nodiscard]] float yaw_deg() const { return yaw_; }
  [[nodiscard]] float pitch_deg() const { return mode_ == CameraMode::Isometric ? s_.iso_pitch_deg : pitch_; }
  [[nodiscard]] int yaw_step() const { return target_yaw_step_; }
  [[nodiscard]] float zoom() const { return zoom_; }
  [[nodiscard]] float distance() const { return dist_; }
  [[nodiscard]] const glm::vec3& target() const { return target_; }

  // Hướng (đơn vị) từ cảnh tới camera = ngược hướng nhìn.
  [[nodiscard]] glm::vec3 back_dir() const {
    const float p = glm::radians(pitch_deg()), y = glm::radians(yaw_);
    return glm::normalize(glm::vec3(std::sin(y) * std::cos(p), std::sin(p), std::cos(y) * std::cos(p)));
  }
  [[nodiscard]] glm::vec3 look_dir() const { return -back_dir(); }
  // Điểm camera nhìn vào
  [[nodiscard]] glm::vec3 pivot() const {
    switch (mode_) {
      case CameraMode::ThirdPerson: return target_ + glm::vec3(0, s_.tp_pivot_height, 0);
      case CameraMode::FirstPerson: return eye();
      default: return target_;
    }
  }
  [[nodiscard]] glm::vec3 position() const {
    switch (mode_) {
      case CameraMode::ThirdPerson: {
        glm::vec3 p = pivot() + back_dir() * dist_;
        if (ground_) p.y = std::max(p.y, ground_(p.x, p.z) + 0.35f);
        return p;
      }
      case CameraMode::FirstPerson: return eye();
      default: return target_ + back_dir() * s_.iso_distance;
    }
  }
  // Hướng "tiến"/"phải" trên mặt đất theo camera — chuyển input WASD/joystick.
  [[nodiscard]] glm::vec3 ground_forward() const {
    const float y = glm::radians(yaw_);
    return glm::vec3(-std::sin(y), 0.0f, -std::cos(y));
  }
  [[nodiscard]] glm::vec3 ground_right() const {
    const float y = glm::radians(yaw_);
    return glm::vec3(std::cos(y), 0.0f, -std::sin(y));
  }

  [[nodiscard]] glm::mat4 view() const {
    if (mode_ == CameraMode::FirstPerson) return glm::lookAt(eye(), eye() + look_dir(), glm::vec3(0, 1, 0));
    return glm::lookAt(position(), pivot(), glm::vec3(0, 1, 0));
  }
  [[nodiscard]] glm::mat4 proj(float aspect) const {
    glm::mat4 p;
    if (mode_ == CameraMode::Isometric) {
      const float hh = zoom_, hw = zoom_ * aspect;
      p = glm::orthoRH_ZO(-hw, hw, -hh, hh, 0.1f, s_.iso_distance * 2.5f);
    } else {
      const float fov = mode_ == CameraMode::FirstPerson ? s_.fp_fov_deg : s_.tp_fov_deg;
      p = glm::perspectiveRH_ZO(glm::radians(fov), std::max(aspect, 0.1f), mode_ == CameraMode::FirstPerson ? 0.05f : 0.1f, s_.far_plane);
    }
    p[1][1] *= -1.0f;  // Vulkan: Y xuống
    return p;
  }

 private:
  [[nodiscard]] glm::vec3 eye() const {
    return target_ + glm::vec3(0, s_.fp_eye_height, 0) + ground_forward() * s_.fp_eye_forward;
  }
  CameraSettings s_;
  CameraMode mode_;
  glm::vec3 target_{0.0f}, goal_{0.0f};
  float yaw_ = 0.0f, yaw_goal_ = 0.0f;
  float pitch_ = 14.0f;  // góc thứ ba: hơi nhìn xuống
  int target_yaw_step_ = 0;
  float zoom_;
  float dist_;
  std::function<float(float, float)> ground_;
};

// Camera 2.5D cũ (ADR-002) — giữ tương thích cho test và chế độ bản đồ.
class Camera25D : public GameCamera {
 public:
  Camera25D() : GameCamera(CameraMode::Isometric) {}
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
