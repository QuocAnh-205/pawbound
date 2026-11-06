#pragma once
// Hình học 2D số nguyên (cm) cho generator và validator.
#include <algorithm>
#include <cstdint>

#include "world/world_types.hpp"

namespace pb::world {

struct Rect {
  cm x0 = 0, z0 = 0, x1 = 0, z1 = 0;  // x0 <= x1, z0 <= z1
  [[nodiscard]] cm w() const { return x1 - x0; }
  [[nodiscard]] cm d() const { return z1 - z0; }
  [[nodiscard]] cm cx() const { return (x0 + x1) / 2; }
  [[nodiscard]] cm cz() const { return (z0 + z1) / 2; }
  [[nodiscard]] bool valid() const { return x1 > x0 && z1 > z0; }
  [[nodiscard]] Rect inset(cm m) const { return {x0 + m, z0 + m, x1 - m, z1 - m}; }
  [[nodiscard]] bool contains(cm x, cm z) const { return x >= x0 && x <= x1 && z >= z0 && z <= z1; }
  // Giao nhau với diện tích dương (chạm cạnh không tính).
  [[nodiscard]] bool overlaps(const Rect& o) const { return x0 < o.x1 && o.x0 < x1 && z0 < o.z1 && o.z0 < z1; }
};

inline Rect building_rect(const Building& b) { return {b.x - b.half_x, b.z - b.half_z, b.x + b.half_x, b.z + b.half_z}; }

inline int64_t dist2(cm ax, cm az, cm bx, cm bz) {
  const int64_t dx = ax - bx, dz = az - bz;
  return dx * dx + dz * dz;
}

// Khoảng cách bình phương từ điểm tới đoạn thẳng (cm^2), tính bằng double nhưng chỉ dùng để so sánh ngưỡng.
inline double point_segment_dist2(double px, double pz, double ax, double az, double bx, double bz) {
  const double vx = bx - ax, vz = bz - az;
  const double len2 = vx * vx + vz * vz;
  double t = len2 > 0 ? ((px - ax) * vx + (pz - az) * vz) / len2 : 0.0;
  t = std::clamp(t, 0.0, 1.0);
  const double dx = ax + t * vx - px, dz = az + t * vz - pz;
  return dx * dx + dz * dz;
}

// Khoảng cách bình phương từ điểm tới hình chữ nhật (0 nếu ở trong).
inline int64_t point_rect_dist2(cm x, cm z, const Rect& r) {
  const int64_t dx = x < r.x0 ? r.x0 - x : (x > r.x1 ? x - r.x1 : 0);
  const int64_t dz = z < r.z0 ? r.z0 - z : (z > r.z1 ? z - r.z1 : 0);
  return dx * dx + dz * dz;
}

// Đoạn thẳng có đi xuyên qua hình chữ nhật (lấy mẫu mỗi step cm) không.
inline bool segment_hits_rect(cm ax, cm az, cm bx, cm bz, const Rect& r, cm step = 50) {
  const int64_t len = static_cast<int64_t>(std::max(std::abs(bx - ax), std::abs(bz - az)));
  const int64_t n = std::max<int64_t>(1, len / step);
  for (int64_t i = 0; i <= n; ++i) {
    const cm x = static_cast<cm>(ax + (bx - ax) * i / n);
    const cm z = static_cast<cm>(az + (bz - az) * i / n);
    if (x > r.x0 && x < r.x1 && z > r.z0 && z < r.z1) return true;
  }
  return false;
}

// Hình chữ nhật lòng đường (with_sidewalk = cả vỉa hè) của một cạnh đường thẳng trục.
inline Rect road_rect(const RoadNode& a, const RoadNode& b, cm half) {
  return {std::min(a.x, b.x) - half, std::min(a.z, b.z) - half, std::max(a.x, b.x) + half, std::max(a.z, b.z) + half};
}

}  // namespace pb::world
