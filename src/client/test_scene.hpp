#pragma once
// Scene kiểm thử renderer (Phase 3): sinh hoàn toàn bằng code, không có file level.
// Tất cả mesh là placeholder procedural — KHÔNG phải asset production.
#include <filesystem>
#include <vector>

#include "render/renderer.hpp"

namespace pb::client {

struct TestScene {
  std::vector<render::DrawItem> items;
  std::vector<render::PointLight> lights;
  glm::vec3 focus{0.0f};
};

TestScene build_test_scene(render::Renderer& r, const std::filesystem::path& asset_root);

}  // namespace pb::client
