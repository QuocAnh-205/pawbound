#include "client/model_upload.hpp"

#include <format>

#include <glm/gtc/matrix_transform.hpp>

#include "core/log.hpp"

namespace pb::client {

GpuModel upload_model(render::Renderer& r, const asset::Model& m, const std::string& prefix, float scale) {
  GpuModel gm;
  gm.id = prefix;
  gm.scale = scale;
  gm.bounds = m.bounds;
  std::vector<uint32_t> tex(m.images.size(), render::kTexWhite);
  for (size_t i = 0; i < m.images.size(); ++i) {
    const auto& img = m.images[i];
    // Ảnh dùng cho cả màu và dữ liệu là bất thường; ưu tiên sRGB và cảnh báo.
    if (img.used_as_color && img.used_as_data)
      log::warn("asset", "{}: ảnh '{}' vừa là màu vừa là dữ liệu", prefix, img.name);
    tex[i] = r.upload_texture(img.image, img.used_as_color, std::format("{}/img{}", prefix, i));
  }
  auto tex_of = [&](int idx, uint32_t def) { return idx >= 0 ? tex[static_cast<size_t>(idx)] : def; };

  for (size_t ni = 0; ni < m.nodes.size(); ++ni) {
    const auto& node = m.nodes[ni];
    if (node.mesh < 0) continue;
    const auto& mesh = m.meshes[static_cast<size_t>(node.mesh)];
    const glm::mat4 node_world = node.skin >= 0 ? glm::mat4(1.0f) : m.node_world(static_cast<int>(ni));
    for (size_t pi = 0; pi < mesh.primitives.size(); ++pi) {
      const auto& prim = mesh.primitives[pi];
      GpuModelPart part;
      const auto name = std::format("{}/{}/{}", prefix, mesh.name, pi);
      part.mesh = prim.joints.empty() ? r.upload_mesh(prim.mesh, name)
                                      : r.upload_skinned_mesh(prim.mesh, prim.joints, prim.weights, name);
      part.node_transform = node_world;
      part.primitive_skin = prim.joints.empty() ? -1 : node.skin;
      if (prim.material >= 0) {
        const auto& mat = m.materials[static_cast<size_t>(prim.material)];
        part.material.base_color = mat.base_color;
        part.material.metallic = mat.metallic;
        part.material.roughness = mat.roughness;
        part.material.base_tex = tex_of(mat.base_image, render::kTexWhite);
        part.material.normal_tex = tex_of(mat.normal_image, render::kTexFlatNormal);
        part.material.orm_tex = tex_of(mat.orm_image, render::kTexWhite);
        part.material.emissive = (mat.emissive.r + mat.emissive.g + mat.emissive.b) / 3.0f;
      }
      gm.parts.push_back(part);
    }
  }
  return gm;
}

void append_draws(const GpuModel& gm, const glm::mat4& world, std::vector<render::DrawItem>& out) {
  const glm::mat4 base = world * glm::scale(glm::mat4(1.0f), glm::vec3(gm.scale));
  for (const auto& p : gm.parts) {
    render::DrawItem it;
    it.mesh = p.mesh;
    it.transform = base * p.node_transform;
    it.material = p.material;
    out.push_back(it);
  }
}

}  // namespace pb::client
