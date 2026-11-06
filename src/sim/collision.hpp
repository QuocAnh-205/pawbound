#pragma once
// Va chạm 2.5D cho nhân vật (ADR-006 phương án dự phòng): hình học tĩnh trên mặt phẳng XZ + chiều cao.
// Deterministic, không phụ thuộc thư viện vật lý; chạy được trên dedicated server.
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/vec2.hpp>

#include "world/world_types.hpp"

namespace pb::sim {

struct StaticBox {
  glm::vec2 min{0.0f}, max{0.0f};
  float top = 0.0f;  // độ cao đỉnh (m, tuyệt đối)
};
struct StaticCircle {
  glm::vec2 center{0.0f};
  float radius = 0.0f;
  float top = 0.0f;  // độ cao đỉnh; hồ nước = +vô cực (không lội qua được)
};

struct CollisionHit {
  bool hit = false;
  glm::vec2 normal{0.0f};  // pháp tuyến tổng hợp hướng ra khỏi vật cản
};

class CollisionWorld {
 public:
  explicit CollisionWorld(const world::WorldData& w);
  // Thêm hình (dùng cho test và vật thể động sau này).
  void add_box(const StaticBox& b);
  void add_circle(const StaticCircle& c);

  // Đẩy hình tròn (tâm pos, bán kính r, chân ở độ cao feet) ra khỏi mọi vật cản cao hơn feet + step.
  CollisionHit resolve(glm::vec2& pos, float radius, float feet, float step_height) const;
  [[nodiscard]] bool overlaps(glm::vec2 pos, float radius, float feet, float step_height) const;
  [[nodiscard]] float ground_height(float x, float z) const;
  // Tầm nhìn: đoạn a->b ở độ cao eye bị vật cản (box cao hơn eye) chắn không. Lấy mẫu mỗi 0,5 m.
  [[nodiscard]] bool segment_blocked(glm::vec2 a, glm::vec2 b, float eye) const;
  [[nodiscard]] glm::vec2 world_size() const { return size_; }
  [[nodiscard]] size_t box_count() const { return boxes_.size(); }
  [[nodiscard]] size_t circle_count() const { return circles_.size(); }

 private:
  void index_shape(glm::vec2 mn, glm::vec2 mx, uint32_t id);
  template <class F>
  void query(glm::vec2 mn, glm::vec2 mx, F&& f) const;

  const world::WorldData* world_ = nullptr;
  glm::vec2 size_{0.0f};
  float cell_ = 8.0f;
  std::vector<StaticBox> boxes_;
  std::vector<StaticCircle> circles_;
  // id: bit 31 = circle
  std::unordered_map<uint64_t, std::vector<uint32_t>> grid_;
  mutable std::vector<uint32_t> stamp_;
  mutable uint32_t query_id_ = 0;
};

}  // namespace pb::sim
