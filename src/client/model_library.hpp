#pragma once
// Nạp mỗi model (theo asset_id trong manifest) đúng MỘT lần, dùng chung cho mọi instance
// (người chơi, chó NPC...). Tránh nạp glTF lặp và vượt giới hạn slot texture của renderer.
#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include "asset/manifest.hpp"
#include "asset/model.hpp"
#include "client/model_upload.hpp"

namespace pb::client {

struct LoadedModel {
  std::string asset_id;
  std::string quality;  // placeholder | prototype | production
  asset::Model cpu;
  GpuModel gpu;  // scale = 1 (instance tự nhân scale)
};

class ModelLibrary {
 public:
  ModelLibrary(render::Renderer& r, const std::filesystem::path& asset_root);
  // nullptr nếu không có trong manifest hoặc nạp lỗi (đã log).
  const LoadedModel* get(const std::string& asset_id);

 private:
  render::Renderer& r_;
  asset::Manifest manifest_;
  std::map<std::string, std::unique_ptr<LoadedModel>> cache_;
  std::map<std::string, bool> failed_;
};

}  // namespace pb::client
