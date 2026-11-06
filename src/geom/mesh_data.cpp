#include "geom/mesh_data.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/matrix.hpp>

namespace pb::geom {

void Aabb::expand(const glm::vec3& p) {
  min = glm::min(min, p);
  max = glm::max(max, p);
}

void Aabb::expand(const Aabb& b) {
  if (!b.valid()) return;
  expand(b.min);
  expand(b.max);
}

Aabb Aabb::transformed(const glm::mat4& m) const {
  if (!valid()) return {};
  Aabb out;
  const glm::vec3 t(m[3]);
  out.min = out.max = t;
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      const float a = m[j][i] * min[j];
      const float b = m[j][i] * max[j];
      out.min[i] += std::min(a, b);
      out.max[i] += std::max(a, b);
    }
  }
  return out;
}

void MeshData::recompute_bounds() {
  bounds = {};
  for (const auto& v : vertices) bounds.expand(v.position);
}

void MeshData::generate_tangents() {
  std::vector<glm::vec3> tan(vertices.size(), glm::vec3(0.0f));
  std::vector<glm::vec3> bit(vertices.size(), glm::vec3(0.0f));
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    const uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
    const auto &v0 = vertices[i0], &v1 = vertices[i1], &v2 = vertices[i2];
    const glm::vec3 e1 = v1.position - v0.position, e2 = v2.position - v0.position;
    const glm::vec2 d1 = v1.uv - v0.uv, d2 = v2.uv - v0.uv;
    const float det = d1.x * d2.y - d2.x * d1.y;
    if (std::abs(det) < 1e-12f) continue;
    const float r = 1.0f / det;
    const glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r;
    const glm::vec3 b = (e2 * d1.x - e1 * d2.x) * r;
    for (uint32_t k : {i0, i1, i2}) {
      tan[k] += t;
      bit[k] += b;
    }
  }
  for (size_t i = 0; i < vertices.size(); ++i) {
    const glm::vec3 n = vertices[i].normal;
    glm::vec3 t = tan[i] - n * glm::dot(n, tan[i]);
    if (glm::dot(t, t) < 1e-12f) {
      // UV suy biến: chọn vector vuông góc bất kỳ
      t = std::abs(n.x) < 0.9f ? glm::cross(n, glm::vec3(1, 0, 0)) : glm::cross(n, glm::vec3(0, 1, 0));
    }
    t = glm::normalize(t);
    const float w = glm::dot(glm::cross(n, t), bit[i]) < 0.0f ? -1.0f : 1.0f;
    vertices[i].tangent = glm::vec4(t, w);
  }
}

void MeshData::append(const MeshData& o, const glm::mat4& m, const glm::vec4& color) {
  const auto base = static_cast<uint32_t>(vertices.size());
  const glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(m)));
  const glm::mat3 m3(m);
  for (auto v : o.vertices) {
    v.position = glm::vec3(m * glm::vec4(v.position, 1.0f));
    v.normal = glm::normalize(nm * v.normal);
    v.tangent = glm::vec4(glm::normalize(m3 * glm::vec3(v.tangent)), v.tangent.w);
    v.color *= color;
    vertices.push_back(v);
    bounds.expand(v.position);
  }
  for (uint32_t i : o.indices) indices.push_back(base + i);
}

std::string validate_mesh(const MeshData& m) {
  if (m.vertices.empty()) return "mesh không có vertex";
  if (m.indices.size() % 3 != 0) return std::format("số index {} không chia hết cho 3", m.indices.size());
  for (size_t i = 0; i < m.indices.size(); ++i)
    if (m.indices[i] >= m.vertices.size())
      return std::format("index[{}]={} vượt số vertex {}", i, m.indices[i], m.vertices.size());
  for (size_t i = 0; i < m.vertices.size(); ++i) {
    const auto& v = m.vertices[i];
    for (int k = 0; k < 3; ++k)
      if (!std::isfinite(v.position[k]) || !std::isfinite(v.normal[k]))
        return std::format("vertex {} có giá trị NaN/Inf", i);
  }
  return {};
}

}  // namespace pb::geom
