#pragma once
// Đưa asset::Model (CPU) lên renderer: texture, mesh, material theo từng primitive.
#include <string>
#include <vector>

#include "asset/model.hpp"
#include "render/renderer.hpp"

namespace pb::client {

struct GpuModelPart {
  render::MeshHandle mesh;
  render::Material material;
  glm::mat4 node_transform{1.0f};  // transform node trong model (bind pose)
  int primitive_skin = -1;         // >= 0: primitive skinned (dùng skeleton model.skins[skin])
};

struct GpuModel {
  std::string id;
  std::vector<GpuModelPart> parts;
  geom::Aabb bounds;
  float scale = 1.0f;
};

// name_prefix dùng để đặt tên resource (mesh/texture) duy nhất trong renderer.
GpuModel upload_model(render::Renderer& r, const asset::Model& m, const std::string& name_prefix, float scale);

// Tạo DrawItem cho một instance của model tại world transform.
void append_draws(const GpuModel& gm, const glm::mat4& world, std::vector<render::DrawItem>& out);

}  // namespace pb::client
