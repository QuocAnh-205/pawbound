#pragma once
// Dữ liệu hình học phía CPU (không phụ thuộc Vulkan). Quy ước: Y lên, 1 unit = 1 m, mặt trước +Z.
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace pb::geom {

struct Vertex {
  glm::vec3 position{0.0f};
  glm::vec3 normal{0.0f, 1.0f, 0.0f};
  glm::vec2 uv{0.0f};
  glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};  // xyz + dấu bitangent (glTF)
  glm::vec4 color{1.0f};                      // vertex color (nhân với base color)
};

struct Aabb {
  glm::vec3 min{std::numeric_limits<float>::max()};
  glm::vec3 max{std::numeric_limits<float>::lowest()};
  [[nodiscard]] bool valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
  void expand(const glm::vec3& p);
  void expand(const Aabb& b);
  [[nodiscard]] glm::vec3 center() const { return (min + max) * 0.5f; }
  [[nodiscard]] glm::vec3 extent() const { return (max - min) * 0.5f; }
  // AABB bao quanh box này sau khi biến đổi bằng m (Arvo).
  [[nodiscard]] Aabb transformed(const glm::mat4& m) const;
};

struct MeshData {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  Aabb bounds;
  void recompute_bounds();
  // Sinh tangent từ UV (khi asset không có tangent). Yêu cầu index list tam giác.
  void generate_tangents();
  // Gộp mesh khác (đã biến đổi bằng m) vào mesh này.
  void append(const MeshData& other, const glm::mat4& m, const glm::vec4& color = glm::vec4(1.0f));
  [[nodiscard]] size_t triangle_count() const { return indices.size() / 3; }
};

// Kiểm tra tính hợp lệ: index trong khoảng, số index chia hết cho 3, normal không NaN.
// Trả chuỗi rỗng nếu hợp lệ, ngược lại mô tả lỗi đầu tiên.
[[nodiscard]] std::string validate_mesh(const MeshData& m);

}  // namespace pb::geom
