#pragma once
// Model nạp từ glTF 2.0 / GLB (qua cgltf). Dữ liệu thuần CPU, không phụ thuộc renderer.
#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "asset/animation.hpp"
#include "geom/image.hpp"
#include "geom/mesh_data.hpp"

namespace pb::asset {

struct ModelMaterial {
  std::string name;
  glm::vec4 base_color{1.0f};
  float metallic = 1.0f;  // mặc định theo glTF
  float roughness = 1.0f;
  glm::vec3 emissive{0.0f};
  int base_image = -1;
  int normal_image = -1;
  int orm_image = -1;  // metallicRoughness (G=roughness, B=metallic); occlusion có thể cùng ảnh
  bool alpha_mask = false;
  bool alpha_blend = false;
  float alpha_cutoff = 0.5f;
  bool double_sided = false;
};

struct ModelPrimitive {
  geom::MeshData mesh;
  int material = -1;
  // Skinning (rỗng nếu không skinned): 4 joint + 4 weight mỗi vertex.
  std::vector<std::array<uint16_t, 4>> joints;
  std::vector<glm::vec4> weights;
};

struct ModelMesh {
  std::string name;
  std::vector<ModelPrimitive> primitives;
};

struct ModelNode {
  std::string name;
  int parent = -1;
  Transform local;
  int mesh = -1;
  int skin = -1;
};

struct ModelImage {
  std::string name;
  geom::ImageRGBA8 image;
  bool used_as_color = false;  // base color/emissive -> sRGB
  bool used_as_data = false;   // normal/ORM -> linear
};

struct Model {
  std::filesystem::path source;
  std::vector<ModelNode> nodes;  // parent luôn đứng trước child
  std::vector<ModelMesh> meshes;
  std::vector<ModelMaterial> materials;
  std::vector<ModelImage> images;
  std::vector<Skeleton> skins;
  std::vector<AnimationClip> animations;  // track.joint chỉ số joint trong skins[0]
  geom::Aabb bounds;                      // bind pose, world space
  std::vector<std::string> warnings;

  [[nodiscard]] glm::mat4 node_world(int node) const;
  [[nodiscard]] size_t triangle_count() const;
  [[nodiscard]] const AnimationClip* find_animation(const std::string& name) const;
};

struct LoadResult {
  Model model;
  std::string error;  // rỗng nếu thành công
  [[nodiscard]] bool ok() const { return error.empty(); }
};

LoadResult load_gltf(const std::filesystem::path& path);

}  // namespace pb::asset
