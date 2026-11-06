#pragma once
// Hiển thị thế giới procedural: streaming chunk quanh nhân vật, mesh gộp theo chunk.
// Toàn bộ hình học là PLACEHOLDER sinh bằng code (asset_ids "placeholder:*").
#include <filesystem>
#include <unordered_map>
#include <vector>

#include "render/renderer.hpp"
#include "world/streaming.hpp"
#include "world/world_types.hpp"

namespace pb::client {

class WorldView {
 public:
  // `world` chỉ cần dữ liệu toàn cục (chunks có thể rỗng — sẽ nạp từ dir khi stream).
  WorldView(render::Renderer& r, world::WorldData world, std::filesystem::path dir);
  ~WorldView();
  WorldView(const WorldView&) = delete;
  WorldView& operator=(const WorldView&) = delete;

  // Nạp/gỡ chunk quanh vị trí (m). Trả số chunk vừa nạp.
  int update(const glm::vec3& focus);
  void collect(std::vector<render::DrawItem>& out, std::vector<render::PointLight>& lights) const;
  [[nodiscard]] const world::WorldData& data() const { return world_; }
  [[nodiscard]] size_t loaded_chunks() const { return chunks_.size(); }
  [[nodiscard]] glm::vec3 spawn_point() const;
  // Chunk đang nạp (dữ liệu logic) — dùng cho gameplay client-side (tương tác, va chạm cục bộ).
  [[nodiscard]] const world::ChunkData* chunk(int index) const;

 private:
  struct ChunkView {
    world::ChunkData data;
    std::vector<render::MeshHandle> meshes;
    std::vector<render::DrawItem> items;
    std::vector<render::PointLight> lights;
  };
  void load_chunk(int index);
  void unload_chunk(int index);
  void build_global();

  render::Renderer& r_;
  world::WorldData world_;
  std::filesystem::path dir_;
  world::ChunkStreamer streamer_;
  std::unordered_map<int, ChunkView> chunks_;
  std::vector<render::DrawItem> global_items_;
  std::vector<render::MeshHandle> global_meshes_;
  uint32_t tex_ground_ = 0, tex_wall_ = 0, tex_road_ = 0;
  uint64_t mesh_serial_ = 0;
};

}  // namespace pb::client
