#include "client/model_library.hpp"

#include "core/log.hpp"

namespace pb::client {

ModelLibrary::ModelLibrary(render::Renderer& r, const std::filesystem::path& asset_root) : r_(r) {
  ConfigDiagnostics d;
  manifest_ = asset::load_manifest(asset_root / "assets/manifest.json", d);
  for (const auto& e : d.errors) log::warn("asset", "manifest: {}", e);
}

const LoadedModel* ModelLibrary::get(const std::string& id) {
  if (auto it = cache_.find(id); it != cache_.end()) return it->second.get();
  if (failed_.contains(id)) return nullptr;
  const auto* entry = manifest_.find(id);
  if (!entry) {
    log::warn("asset", "'{}' không có trong manifest", id);
    failed_[id] = true;
    return nullptr;
  }
  auto lr = asset::load_gltf(manifest_.base_dir / entry->path);
  if (!lr.ok()) {
    log::warn("asset", "nạp '{}' lỗi: {}", id, lr.error);
    failed_[id] = true;
    return nullptr;
  }
  auto m = std::make_unique<LoadedModel>();
  m->asset_id = id;
  m->quality = entry->quality;
  m->cpu = std::move(lr.model);
  m->gpu = upload_model(r_, m->cpu, "model/" + id, 1.0f);
  log::info("asset", "đã nạp '{}' ({}, {} tam giác) — dùng chung cho mọi instance", id, m->quality, m->cpu.triangle_count());
  return cache_.emplace(id, std::move(m)).first->second.get();
}

}  // namespace pb::client
