#pragma once
// Cấu hình khởi động client. Thiếu field -> dùng mặc định (cảnh báo); sai kiểu/khoảng -> lỗi.
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace pb {

struct ClientConfig {
  int window_width = 1280;
  int window_height = 720;
  std::string window_title = "PAWBOUND";
  bool vsync = true;
  std::string log_level = "info";
  std::string log_file = "logs/client.log";
};

struct ClientConfigResult {
  ClientConfig config;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
  [[nodiscard]] bool ok() const { return errors.empty(); }
};

[[nodiscard]] ClientConfigResult parse_client_config(std::string_view json_text);
[[nodiscard]] ClientConfigResult load_client_config(const std::filesystem::path& path);

}  // namespace pb
