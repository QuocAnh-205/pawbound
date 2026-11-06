#include "geom/image.hpp"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <fstream>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace pb::geom {

ImageRGBA8 decode_image(std::span<const uint8_t> bytes, std::string& error) {
  ImageRGBA8 img;
  int w = 0, h = 0, n = 0;
  stbi_uc* data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &n, 4);
  if (!data) {
    error = stbi_failure_reason() ? stbi_failure_reason() : "không giải mã được ảnh";
    return img;
  }
  img.width = w;
  img.height = h;
  img.pixels.assign(data, data + static_cast<size_t>(w) * h * 4);
  stbi_image_free(data);
  return img;
}

ImageRGBA8 load_image(const std::filesystem::path& path, std::string& error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "không mở được " + path.string();
    return {};
  }
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return decode_image(bytes, error);
}

namespace {
void write_cb(void* ctx, void* data, int size) {
  static_cast<std::ofstream*>(ctx)->write(static_cast<const char*>(data), size);
}
}  // namespace

bool write_png(const std::filesystem::path& path, const ImageRGBA8& img) {
  if (!img.valid()) return false;
  std::error_code ec;
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  const int ok = stbi_write_png_to_func(write_cb, &out, img.width, img.height, 4, img.pixels.data(), img.width * 4);
  return ok != 0 && out.good();
}

std::vector<uint8_t> encode_png(const ImageRGBA8& img) {
  std::vector<uint8_t> out;
  if (!img.valid()) return out;
  auto cb = [](void* ctx, void* data, int size) {
    auto* v = static_cast<std::vector<uint8_t>*>(ctx);
    const auto* b = static_cast<const uint8_t*>(data);
    v->insert(v->end(), b, b + size);
  };
  if (!stbi_write_png_to_func(cb, &out, img.width, img.height, 4, img.pixels.data(), img.width * 4)) out.clear();
  return out;
}

ImageRGBA8 make_checker(int size, int cells, uint32_t a, uint32_t b) {
  ImageRGBA8 img;
  img.width = img.height = size;
  img.pixels.resize(static_cast<size_t>(size) * size * 4);
  const int cell = std::max(1, size / std::max(1, cells));
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const uint32_t c = (((x / cell) + (y / cell)) & 1) ? a : b;
      uint8_t* p = &img.pixels[(static_cast<size_t>(y) * size + x) * 4];
      p[0] = static_cast<uint8_t>(c >> 24);
      p[1] = static_cast<uint8_t>(c >> 16);
      p[2] = static_cast<uint8_t>(c >> 8);
      p[3] = static_cast<uint8_t>(c);
    }
  }
  return img;
}

ImageRGBA8 make_solid(int size, uint32_t rgba) { return make_checker(size, 1, rgba, rgba); }

}  // namespace pb::geom
