#include "core/client_config.hpp"

#include "core/config_reader.hpp"
#include "core/log.hpp"

namespace pb {
namespace {
ClientConfigResult from_json(const nlohmann::json& j, ConfigDiagnostics diag) {
  ClientConfigResult r;
  ConfigReader rd(j, "", diag);
  auto& c = r.config;
  if (diag.ok()) {
    rd.read("window_width", c.window_width, 320, 7680);
    rd.read("window_height", c.window_height, 240, 4320);
    rd.read("window_title", c.window_title);
    rd.read("vsync", c.vsync);
    rd.read("log_level", c.log_level);
    rd.read("log_file", c.log_file);
    if (!log::parse_level(c.log_level)) diag.errors.push_back("'log_level' không hợp lệ: " + c.log_level);
  }
  r.errors = std::move(diag.errors);
  r.warnings = std::move(diag.warnings);
  return r;
}
}  // namespace

ClientConfigResult parse_client_config(std::string_view text) {
  ConfigDiagnostics diag;
  auto j = parse_json_config(text, diag);
  return from_json(j, std::move(diag));
}

ClientConfigResult load_client_config(const std::filesystem::path& path) {
  ConfigDiagnostics diag;
  auto j = load_json_config(path, diag, /*required=*/false);
  return from_json(j, std::move(diag));
}

}  // namespace pb
