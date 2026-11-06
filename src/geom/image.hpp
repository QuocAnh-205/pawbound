#pragma once
// Ảnh RGBA8 phía CPU: sinh texture bằng code, đọc PNG/JPEG (stb_image), ghi PNG (screenshot).
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace pb::geom {

struct ImageRGBA8 {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels;  // width*height*4, hàng trên cùng trước
  [[nodiscard]] bool valid() const {
    return width > 0 && height > 0 && pixels.size() == static_cast<size_t>(width) * height * 4;
  }
};

// Giải mã ảnh từ bộ nhớ (PNG/JPEG/TGA/BMP). Lỗi -> ảnh rỗng + error.
ImageRGBA8 decode_image(std::span<const uint8_t> bytes, std::string& error);
ImageRGBA8 load_image(const std::filesystem::path& path, std::string& error);

// Mã hóa PNG (deflate do stb_image_write). Trả false nếu lỗi ghi.
bool write_png(const std::filesystem::path& path, const ImageRGBA8& img);
// Mã hóa PNG vào bộ nhớ (rỗng nếu lỗi).
std::vector<uint8_t> encode_png(const ImageRGBA8& img);

// Texture sinh bằng code cho placeholder/test.
ImageRGBA8 make_checker(int size, int cells, uint32_t rgba_a, uint32_t rgba_b);
ImageRGBA8 make_solid(int size, uint32_t rgba);

}  // namespace pb::geom
