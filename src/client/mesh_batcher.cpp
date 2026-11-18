#include "client/mesh_batcher.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>

namespace pb::client {

void MeshBatcher::add(const LoadedModel& lm, const glm::mat4& world, glm::vec4 tint, float wind) {
  const auto& m = lm.cpu;
  const float y0 = m.bounds.min.y, hgt = std::max(m.bounds.max.y - m.bounds.min.y, 0.01f);
  size_t part = 0;  // thứ tự giống upload_model: node -> primitive
  for (size_t ni = 0; ni < m.nodes.size(); ++ni) {
    const auto& node = m.nodes[ni];
    if (node.mesh < 0) continue;
    const auto& mesh = m.meshes[static_cast<size_t>(node.mesh)];
    const glm::mat4 nw = m.node_world(static_cast<int>(ni));
    for (const auto& prim : mesh.primitives) {
      if (part >= lm.gpu.parts.size()) return;
      const auto& gp = lm.gpu.parts[part++];
      if (!prim.joints.empty()) continue;  // model có xương không gộp được
      render::Material mat = gp.material;
      mat.wind = wind;
      mat.cutaway = true;
      const glm::vec4 col = mat.base_color * tint;
      mat.base_color = glm::vec4(1.0f);  // đã nướng vào vertex
      const Key key{mat.base_tex, mat.normal_tex, mat.orm_tex, static_cast<int>(mat.metallic * 100), static_cast<int>(mat.roughness * 100),
                    static_cast<int>(mat.alpha_cutoff * 100), mat.double_sided, static_cast<int>(wind * 100), mat.unlit};
      auto& g = groups_[key];
      g.mat = mat;
      const glm::mat4 M = world * nw;
      const glm::mat3 N = glm::transpose(glm::inverse(glm::mat3(M)));
      const glm::mat3 M3(M);
      const auto base = static_cast<uint32_t>(g.mesh.vertices.size());
      for (const auto& v : prim.mesh.vertices) {
        geom::Vertex o = v;
        o.position = glm::vec3(M * glm::vec4(v.position, 1.0f));
        o.normal = glm::normalize(N * v.normal);
        o.tangent = glm::vec4(glm::normalize(M3 * glm::vec3(v.tangent)), v.tangent.w);
        o.color = v.color * col;
        if (wind > 0.0f) {
          const float wy = (glm::vec3(nw * glm::vec4(v.position, 1.0f)).y - y0) / hgt;
          o.color.a = std::pow(std::clamp(wy, 0.0f, 1.0f), 1.6f);  // gốc đứng yên, ngọn lay mạnh
        }
        g.mesh.vertices.push_back(o);
      }
      for (uint32_t i : prim.mesh.indices) g.mesh.indices.push_back(base + i);
    }
  }
}

void MeshBatcher::flush(render::Renderer& r, const std::string& name, std::vector<render::DrawItem>& items,
                        std::vector<render::MeshHandle>& handles, bool cast_shadow) {
  int n = 0;
  for (auto& [k, g] : groups_) {
    if (g.mesh.indices.empty()) continue;
    g.mesh.recompute_bounds();
    auto h = r.upload_mesh(g.mesh, std::format("{}/b{}", name, n++));
    handles.push_back(h);
    items.push_back({h, glm::mat4(1.0f), g.mat, cast_shadow});
  }
  groups_.clear();
}

size_t MeshBatcher::triangles() const {
  size_t t = 0;
  for (const auto& [k, g] : groups_) t += g.mesh.indices.size() / 3;
  return t;
}

}  // namespace pb::client
