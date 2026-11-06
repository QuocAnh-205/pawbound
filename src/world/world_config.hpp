#pragma once
// Cấu hình sinh thế giới (data/world/*.json). Xem docs/architecture/world-generation-design.md.
#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "core/config_reader.hpp"
#include "world/world_types.hpp"

namespace pb::world {

struct WeightedAsset {
  std::string id;
  int weight = 1;
};

// (category) -> danh sách asset có trọng số. Category: "building.downtown", "tree", ...
using AssetRules = std::map<std::string, std::vector<WeightedAsset>>;

struct WorldConfig {
  uint64_t seed = 20261106;
  int size_m_x = 512, size_m_z = 512;
  int chunk_size_m = 64;
  // địa hình
  int terrain_cell_m = 4;
  double amplitude_m = 2.5;
  int noise_period_m = 160;
  int noise_octaves = 3;
  // district
  int district_seeds = 7;
  std::array<int, kDistrictCount> district_weights{25, 40, 20, 15};  // downtown, residential, park, industrial
  std::array<ZoneRule, kDistrictCount> zone_rules{ZoneRule::Restricted, ZoneRule::Restricted, ZoneRule::Safe,
                                                  ZoneRule::Contested};
  // đường
  int margin_m = 16;
  int arterial_spacing_m = 160;
  int arterial_jitter_m = 8;
  int local_spacing_min_m = 48, local_spacing_max_m = 72;
  int arterial_half_width_cm = 500, local_half_width_cm = 350, path_half_width_cm = 150;
  int sidewalk_cm = 200;
  // nhà
  int setback_front_cm = 300, setback_side_cm = 100;
  int empty_lot_percent_residential = 15;
  // cây/props
  int park_tree_spacing_m = 6;
  int lamp_spacing_m = 24;
  int street_tree_spacing_m = 12;
  // POI
  int water_min = 2, water_max = 4;
  int food_min = 3, food_max = 6;
  int shelter_min = 2, shelter_max = 3;
  int spawn_count = 4;

  AssetRules asset_rules;
};

// Đọc file config. Tham số seed_override >= 0 ghi đè seed trong file.
WorldConfig load_world_config(const std::filesystem::path& path, ConfigDiagnostics& diag);
WorldConfig parse_world_config(std::string_view json_text, ConfigDiagnostics& diag);
AssetRules default_asset_rules();
// Hash nội dung config đã chuẩn hóa (để ghi vào world.json).
std::string world_config_hash(const WorldConfig& cfg);

}  // namespace pb::world
