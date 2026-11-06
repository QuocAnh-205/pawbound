#pragma once
// SHA-256 (FIPS 180-4) — CHỈ dùng làm checksum toàn vẹn nội dung (asset hash, world logical hash),
// KHÔNG dùng cho mục đích bảo mật (chữ ký, mật khẩu, MAC). Bảo mật dùng libsodium (ADR-004).
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace pb::asset {

class Sha256 {
 public:
  Sha256();
  void update(std::span<const uint8_t> data);
  void update(std::string_view s) { update({reinterpret_cast<const uint8_t*>(s.data()), s.size()}); }
  template <class T>
  void update_pod(const T& v) {
    update({reinterpret_cast<const uint8_t*>(&v), sizeof(T)});
  }
  std::array<uint8_t, 32> finish();
  std::string finish_hex();

 private:
  void block(const uint8_t* p);
  std::array<uint32_t, 8> h_;
  std::array<uint8_t, 64> buf_{};
  size_t buf_len_ = 0;
  uint64_t total_ = 0;
};

std::string sha256_hex(std::span<const uint8_t> data);
std::optional<std::string> sha256_file_hex(const std::filesystem::path& path);

}  // namespace pb::asset
