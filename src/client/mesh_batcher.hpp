#pragma once
// Gộp nhiều instance model tĩnh (mô-đun nhà, cây, cỏ, đá...) thành ÍT mesh lớn theo từng vật liệu.
// Hàng nghìn ngọn cỏ/mô-đun tường -> vài draw call mỗi chunk. Màu vật liệu (base color) được nướng vào vertex color.
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "client/model_library.hpp"
#include "render/renderer.hpp"

namespace pb::client {

class MeshBatcher {
 public:
  // wind > 0: lay theo gió; trọng số lay = độ cao trong model (0 ở gốc -> 1 ở ngọn) — nướng vào vertex color.a.
  void add(const LoadedModel& m, const glm::mat4& world, glm::vec4 tint = glm::vec4(1.0f), float wind = 0.0f);
  // Upload các mesh gộp; trả DrawItem (đã có material) và handle để giải phóng.
  void flush(render::Renderer& r, const std::string& name, std::vector<render::DrawItem>& items,
             std::vector<render::MeshHandle>& handles, bool cast_shadow = true);
  [[nodiscard]] bool empty() const { return groups_.empty(); }
  [[nodiscard]] size_t triangles() const;

 private:
  using Key = std::tuple<uint32_t, uint32_t, uint32_t, int, int, int, bool, int, bool>;
  struct Group {
    render::Material mat;
    geom::MeshData mesh;
  };
  std::map<Key, Group> groups_;
};

}  // namespace pb::client
