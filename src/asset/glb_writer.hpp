#pragma once
// Ghi GLB tối giản: 1 mesh/1 primitive, 1 material (tùy chọn texture PNG nhúng), tùy chọn skin + animation.
// Dùng để: sinh fixture test, xuất placeholder procedural. KHÔNG thay thế công cụ DCC.
#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include "asset/animation.hpp"
#include "geom/image.hpp"
#include "geom/mesh_data.hpp"

namespace pb::asset {

struct GlbDesc {
  std::string name = "mesh";
  geom::MeshData mesh;
  glm::vec4 base_color{1.0f};
  float metallic = 0.0f;
  float roughness = 0.8f;
  const geom::ImageRGBA8* base_texture = nullptr;
  // Skin (tùy chọn): skeleton.parents phải parent-trước-child; joints/weights theo từng vertex.
  const Skeleton* skeleton = nullptr;
  std::vector<std::array<uint16_t, 4>> joints;
  std::vector<glm::vec4> weights;
  std::vector<AnimationClip> animations;  // chỉ Linear/Step
};

bool write_glb(const std::filesystem::path& path, const GlbDesc& desc, std::string& error);

}  // namespace pb::asset
