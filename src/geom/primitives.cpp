#include "geom/primitives.hpp"

#include <cmath>
#include <numbers>

#include <glm/geometric.hpp>

namespace pb::geom {
namespace {
constexpr float kPi = std::numbers::pi_v<float>;

void add_quad(MeshData& m, const glm::vec3& o, const glm::vec3& u, const glm::vec3& v, const glm::vec3& n,
              glm::vec2 uv_size) {
  const auto base = static_cast<uint32_t>(m.vertices.size());
  const glm::vec3 p[4] = {o, o + u, o + u + v, o + v};
  const glm::vec2 t[4] = {{0, uv_size.y}, {uv_size.x, uv_size.y}, {uv_size.x, 0}, {0, 0}};
  for (int i = 0; i < 4; ++i) {
    Vertex vx;
    vx.position = p[i];
    vx.normal = n;
    vx.uv = t[i];
    vx.tangent = glm::vec4(glm::normalize(u), 1.0f);
    m.vertices.push_back(vx);
  }
  // CCW nhìn từ phía normal
  m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}
}  // namespace

MeshData make_box(const glm::vec3& size, float uv_per_meter) {
  MeshData m;
  const glm::vec3 h = size * 0.5f;
  auto uvs = [&](float a, float b) { return uv_per_meter > 0 ? glm::vec2(a, b) * uv_per_meter : glm::vec2(1.0f); };
  // +Z, -Z, +X, -X, +Y, -Y
  add_quad(m, {-h.x, -h.y, h.z}, {size.x, 0, 0}, {0, size.y, 0}, {0, 0, 1}, uvs(size.x, size.y));
  add_quad(m, {h.x, -h.y, -h.z}, {-size.x, 0, 0}, {0, size.y, 0}, {0, 0, -1}, uvs(size.x, size.y));
  add_quad(m, {h.x, -h.y, h.z}, {0, 0, -size.z}, {0, size.y, 0}, {1, 0, 0}, uvs(size.z, size.y));
  add_quad(m, {-h.x, -h.y, -h.z}, {0, 0, size.z}, {0, size.y, 0}, {-1, 0, 0}, uvs(size.z, size.y));
  add_quad(m, {-h.x, h.y, h.z}, {size.x, 0, 0}, {0, 0, -size.z}, {0, 1, 0}, uvs(size.x, size.z));
  add_quad(m, {-h.x, -h.y, -h.z}, {size.x, 0, 0}, {0, 0, size.z}, {0, -1, 0}, uvs(size.x, size.z));
  m.recompute_bounds();
  return m;
}

MeshData make_grid(float sx, float sz, int nx, int nz, float uv_scale) {
  MeshData m;
  for (int z = 0; z <= nz; ++z) {
    for (int x = 0; x <= nx; ++x) {
      Vertex v;
      const float fx = static_cast<float>(x) / static_cast<float>(nx);
      const float fz = static_cast<float>(z) / static_cast<float>(nz);
      v.position = {(fx - 0.5f) * sx, 0.0f, (fz - 0.5f) * sz};
      v.uv = glm::vec2(fx, fz) * uv_scale;
      v.tangent = {1, 0, 0, 1};
      m.vertices.push_back(v);
    }
  }
  const auto row = static_cast<uint32_t>(nx + 1);
  for (int z = 0; z < nz; ++z) {
    for (int x = 0; x < nx; ++x) {
      const uint32_t a = static_cast<uint32_t>(z) * row + static_cast<uint32_t>(x);
      // CCW nhìn từ trên xuống (+Y)
      m.indices.insert(m.indices.end(), {a, a + row, a + row + 1, a, a + row + 1, a + 1});
    }
  }
  m.recompute_bounds();
  return m;
}

MeshData make_cylinder(float r, float h, int seg, bool caps) {
  MeshData m;
  for (int i = 0; i <= seg; ++i) {
    const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(seg);
    const glm::vec3 n(std::sin(a), 0.0f, std::cos(a));
    for (int k = 0; k < 2; ++k) {
      Vertex v;
      v.position = n * r + glm::vec3(0, h * static_cast<float>(k), 0);
      v.normal = n;
      v.uv = {static_cast<float>(i) / static_cast<float>(seg), 1.0f - static_cast<float>(k)};
      v.tangent = glm::vec4(std::cos(a), 0.0f, -std::sin(a), 1.0f);
      m.vertices.push_back(v);
    }
  }
  for (uint32_t i = 0; i < static_cast<uint32_t>(seg); ++i) {
    const uint32_t a = i * 2;
    m.indices.insert(m.indices.end(), {a, a + 2, a + 3, a, a + 3, a + 1});
  }
  if (caps) {
    for (int k = 0; k < 2; ++k) {
      const float y = h * static_cast<float>(k);
      const glm::vec3 n(0, k ? 1.0f : -1.0f, 0);
      const auto center = static_cast<uint32_t>(m.vertices.size());
      Vertex c;
      c.position = {0, y, 0};
      c.normal = n;
      c.uv = {0.5f, 0.5f};
      m.vertices.push_back(c);
      for (int i = 0; i <= seg; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(seg);
        Vertex v;
        v.position = {std::sin(a) * r, y, std::cos(a) * r};
        v.normal = n;
        v.uv = {0.5f + 0.5f * std::sin(a), 0.5f + 0.5f * std::cos(a)};
        m.vertices.push_back(v);
      }
      for (uint32_t i = 0; i < static_cast<uint32_t>(seg); ++i) {
        if (k) m.indices.insert(m.indices.end(), {center, center + 1 + i, center + 2 + i});
        else m.indices.insert(m.indices.end(), {center, center + 2 + i, center + 1 + i});
      }
    }
  }
  m.recompute_bounds();
  return m;
}

MeshData make_cone(float r, float h, int seg) {
  MeshData m;
  const float slope = r / h;
  for (int i = 0; i < seg; ++i) {
    const float a0 = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(seg);
    const float a1 = 2.0f * kPi * static_cast<float>(i + 1) / static_cast<float>(seg);
    const float am = 0.5f * (a0 + a1);
    const auto base = static_cast<uint32_t>(m.vertices.size());
    for (float a : {a0, a1}) {
      Vertex v;
      v.position = {std::sin(a) * r, 0.0f, std::cos(a) * r};
      v.normal = glm::normalize(glm::vec3(std::sin(a), slope, std::cos(a)));
      m.vertices.push_back(v);
    }
    Vertex tip;
    tip.position = {0, h, 0};
    tip.normal = glm::normalize(glm::vec3(std::sin(am), slope, std::cos(am)));
    m.vertices.push_back(tip);
    m.indices.insert(m.indices.end(), {base, base + 1, base + 2});
  }
  // đáy
  const auto center = static_cast<uint32_t>(m.vertices.size());
  Vertex c;
  c.position = {0, 0, 0};
  c.normal = {0, -1, 0};
  m.vertices.push_back(c);
  for (int i = 0; i <= seg; ++i) {
    const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(seg);
    Vertex v;
    v.position = {std::sin(a) * r, 0, std::cos(a) * r};
    v.normal = {0, -1, 0};
    m.vertices.push_back(v);
  }
  for (uint32_t i = 0; i < static_cast<uint32_t>(seg); ++i)
    m.indices.insert(m.indices.end(), {center, center + 2 + i, center + 1 + i});
  m.recompute_bounds();
  return m;
}

MeshData make_sphere(float r, int rings, int seg) {
  MeshData m;
  for (int y = 0; y <= rings; ++y) {
    const float v = static_cast<float>(y) / static_cast<float>(rings);
    const float phi = v * kPi;
    for (int x = 0; x <= seg; ++x) {
      const float u = static_cast<float>(x) / static_cast<float>(seg);
      const float th = u * 2.0f * kPi;
      Vertex vx;
      vx.normal = {std::sin(phi) * std::sin(th), std::cos(phi), std::sin(phi) * std::cos(th)};
      vx.position = vx.normal * r;
      vx.uv = {u, v};
      vx.tangent = glm::vec4(std::cos(th), 0.0f, -std::sin(th), 1.0f);
      m.vertices.push_back(vx);
    }
  }
  const auto row = static_cast<uint32_t>(seg + 1);
  for (uint32_t y = 0; y < static_cast<uint32_t>(rings); ++y) {
    for (uint32_t x = 0; x < static_cast<uint32_t>(seg); ++x) {
      const uint32_t a = y * row + x;
      m.indices.insert(m.indices.end(), {a, a + row, a + row + 1, a, a + row + 1, a + 1});
    }
  }
  m.recompute_bounds();
  return m;
}

}  // namespace pb::geom
