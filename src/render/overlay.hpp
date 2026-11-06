#pragma once
// Lớp vẽ 2D (HUD, debug): hình chữ nhật màu + chữ bitmap (stb_easy_font, chỉ ASCII).
// Chỉ thu thập đỉnh; renderer vẽ sau tonemap với alpha blend. Tọa độ pixel, gốc trên-trái.
#include <string_view>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace pb::render {

struct OverlayVertex {
  glm::vec2 pos;
  glm::vec4 color;
};

class Overlay {
 public:
  void clear() { verts_.clear(); }
  void rect(float x, float y, float w, float h, const glm::vec4& color);
  // Thanh tiến độ có viền.
  void bar(float x, float y, float w, float h, float fraction, const glm::vec4& fill, const glm::vec4& back);
  // Chữ ASCII (ký tự ngoài ASCII bị thay bằng '?'). scale: hệ số phóng (font gốc cao ~7 px).
  void text(float x, float y, std::string_view s, const glm::vec4& color, float scale = 2.0f);
  [[nodiscard]] static float text_width(std::string_view s, float scale = 2.0f);
  [[nodiscard]] const std::vector<OverlayVertex>& vertices() const { return verts_; }

 private:
  std::vector<OverlayVertex> verts_;
};

}  // namespace pb::render
