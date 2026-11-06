#pragma once
// Asset manifest (assets/manifest.json) và validator. Xem docs/art-pipeline/asset-pipeline-design.md.
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec2.hpp>

#include "core/config_reader.hpp"

namespace pb::asset {

struct ManifestEntry {
  std::string id;
  std::string path;     // tương đối so với thư mục chứa manifest
  std::string type;     // character | building | prop | vegetation | terrain
  std::string quality;  // placeholder | prototype | production
  std::string source;
  std::string author;
  std::string license;  // SPDX, nhiều license nối bằng " AND "
  double scale = 1.0;
  std::string sha256;
  glm::vec2 footprint_m{0.0f};
  double height_m = 0.0;  // chiều cao mong đợi sau scale (0 = không kiểm tra)
  std::vector<std::string> tags;
  std::vector<std::string> required_animations;
};

struct Manifest {
  std::filesystem::path base_dir;
  std::vector<ManifestEntry> entries;
  [[nodiscard]] const ManifestEntry* find(const std::string& id) const;
};

Manifest load_manifest(const std::filesystem::path& path, ConfigDiagnostics& diag);
bool save_manifest(const std::filesystem::path& path, const Manifest& m);

struct Finding {
  bool error = true;  // false = cảnh báo
  std::string asset_id;
  std::string message;
};

[[nodiscard]] bool license_allowed(const std::string& spdx_expr);

// Kiểm tra một asset: license, metadata, file tồn tại, sha256, glTF đọc được, mesh, skeleton,
// animation bắt buộc, số tam giác và kích thước theo loại.
std::vector<Finding> validate_entry(const Manifest& m, const ManifestEntry& e);

}  // namespace pb::asset
