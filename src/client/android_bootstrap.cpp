#include "client/android_bootstrap.hpp"

#if defined(__ANDROID__)
#include <SDL3/SDL.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/config_reader.hpp"
#include "core/log.hpp"
#include "core/version.hpp"
#include "world/generator.hpp"
#include "world/world_io.hpp"
#endif

namespace pb::client {

#if defined(__ANDROID__)
bool is_mobile() { return true; }

namespace {
// SDL_LoadFile với đường dẫn tương đối đọc từ thư mục assets/ trong APK.
bool load_apk(const std::string& rel, std::string& out) {
  size_t n = 0;
  void* d = SDL_LoadFile(rel.c_str(), &n);
  if (!d) return false;
  out.assign(static_cast<const char*>(d), n);
  SDL_free(d);
  return true;
}
}  // namespace

std::vector<std::string> mobile_bootstrap(std::vector<std::string> args, std::string& error) {
  char* pref = SDL_GetPrefPath("Pawbound", "Pawbound");
  if (!pref) {
    error = std::string("SDL_GetPrefPath: ") + SDL_GetError();
    return args;
  }
  const std::filesystem::path root(pref);
  SDL_free(pref);
  // Chép lại khi phiên bản app đổi (cập nhật APK)
  std::string installed;
  std::ifstream(root / ".installed_version") >> installed;
  if (installed != kVersionString) {
    std::string index;
    if (!load_apk("pawbound_files.txt", index)) {
      error = "APK thiếu pawbound_files.txt (chạy android/prepare_assets.py trước khi build)";
      return args;
    }
    std::istringstream is(index);
    std::string rel;
    int copied = 0;
    while (std::getline(is, rel)) {
      if (rel.empty() || rel.find("..") != std::string::npos) continue;
      std::string data;
      if (!load_apk(rel, data)) {
        error = "không đọc được asset trong APK: " + rel;
        return args;
      }
      std::filesystem::create_directories((root / rel).parent_path());
      std::ofstream(root / rel, std::ios::binary).write(data.data(), static_cast<std::streamsize>(data.size()));
      ++copied;
    }
    std::ofstream(root / ".installed_version") << kVersionString;
    log::info("android", "đã chép {} file asset vào {}", copied, root.string());
  }
  if (chdir(root.string().c_str()) != 0) {
    error = "không chdir được vào " + root.string();
    return args;
  }
  // Sinh thế giới lần đầu (tất định: cùng seed -> cùng hash với server)
  if (!std::filesystem::exists("world/logical_hash.txt")) {
    ConfigDiagnostics d;
    auto cfg = world::load_world_config("data/world/demo.json", d);
    if (!d.ok()) {
      error = "config world: " + d.errors.front();
      return args;
    }
    cfg.seed = 20261106;
    const auto w = world::generate_world(cfg);
    if (!world::save_world("world", w, error)) return args;
    log::info("android", "đã sinh thế giới, hash {}", world::logical_hash(w).substr(0, 12));
  }
  auto has = [&](const char* k) { return std::find(args.begin(), args.end(), k) != args.end(); };
  if (!has("--world") && !has("--connect")) args.insert(args.end(), {"--world", "world"});
  if (!has("--asset-root")) args.insert(args.end(), {"--asset-root", "."});
  if (!has("--config")) args.insert(args.end(), {"--config", "config/client.json"});
  return args;
}
#else
bool is_mobile() { return false; }
std::vector<std::string> mobile_bootstrap(std::vector<std::string> args, std::string&) { return args; }
#endif

}  // namespace pb::client
