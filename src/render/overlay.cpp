#include "render/overlay.hpp"

#include <algorithm>
#include <string>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#include <stb_easy_font.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace pb::render {

void Overlay::rect(float x, float y, float w, float h, const glm::vec4& c) {
  const glm::vec2 a(x, y), b(x + w, y), d(x, y + h), e(x + w, y + h);
  verts_.insert(verts_.end(), {{a, c}, {b, c}, {e, c}, {a, c}, {e, c}, {d, c}});
}

void Overlay::bar(float x, float y, float w, float h, float f, const glm::vec4& fill, const glm::vec4& back) {
  rect(x - 1, y - 1, w + 2, h + 2, {0, 0, 0, 0.6f});
  rect(x, y, w, h, back);
  rect(x, y, w * std::clamp(f, 0.0f, 1.0f), h, fill);
}

namespace {
std::string ascii(std::string_view s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('?');
      while (i + 1 < s.size() && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80) ++i;  // bỏ byte tiếp của UTF-8
    }
  }
  return out;
}
}  // namespace

void Overlay::text(float x, float y, std::string_view s, const glm::vec4& c, float scale) {
  std::string a = ascii(s);
  static std::vector<char> buf(1 << 16);
  const int quads = stb_easy_font_print(0, 0, a.data(), nullptr, buf.data(), static_cast<int>(buf.size()));
  struct V {
    float x, y, z;
    unsigned char c[4];
  };
  const auto* v = reinterpret_cast<const V*>(buf.data());
  for (int q = 0; q < quads; ++q) {
    OverlayVertex p[4];
    for (int k = 0; k < 4; ++k) p[k] = {{x + v[q * 4 + k].x * scale, y + v[q * 4 + k].y * scale}, c};
    verts_.insert(verts_.end(), {p[0], p[1], p[2], p[0], p[2], p[3]});
  }
}

float Overlay::text_width(std::string_view s, float scale) {
  std::string a = ascii(s);
  return static_cast<float>(stb_easy_font_width(a.data())) * scale;
}

}  // namespace pb::render
