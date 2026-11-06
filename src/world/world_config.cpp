#include "world/world_config.hpp"

#include <format>

#include "asset/sha256.hpp"

namespace pb::world {

AssetRules default_asset_rules() {
  // Chưa có asset nhà/props production: dùng placeholder procedural (client vẽ bằng hình khối).
  return {
      {"building.downtown", {{"placeholder:building_tower", 3}, {"placeholder:building_shop", 2}}},
      {"building.residential", {{"placeholder:building_house", 4}, {"placeholder:building_apartment", 1}}},
      {"building.industrial", {{"placeholder:building_warehouse", 3}, {"placeholder:building_factory", 1}}},
      {"tree", {{"placeholder:tree_round", 3}, {"placeholder:tree_pine", 1}}},
      {"prop", {{"placeholder:prop", 1}}},
  };
}

namespace {
ZoneRule parse_zone(const std::string& s, ConfigDiagnostics& d) {
  if (s == "safe") return ZoneRule::Safe;
  if (s == "restricted") return ZoneRule::Restricted;
  if (s == "contested") return ZoneRule::Contested;
  d.errors.push_back("zone rule không hợp lệ: " + s);
  return ZoneRule::Restricted;
}

WorldConfig from_json(const nlohmann::json& j, ConfigDiagnostics& d) {
  WorldConfig c;
  ConfigReader r(j, "", d);
  int seed = 0;
  if (r.has("seed")) {
    if (j["seed"].is_number_unsigned() || j["seed"].is_number_integer()) c.seed = j["seed"].get<uint64_t>();
    else d.errors.emplace_back("'seed' phải là số nguyên không âm");
  }
  (void)seed;
  r.read("size_m_x", c.size_m_x, 128, 8192);
  r.read("size_m_z", c.size_m_z, 128, 8192);
  r.read("chunk_size_m", c.chunk_size_m, 16, 512);
  auto t = r.child("terrain");
  t.read("cell_m", c.terrain_cell_m, 1, 32);
  t.read("amplitude_m", c.amplitude_m, 0.0, 50.0);
  t.read("noise_period_m", c.noise_period_m, 8, 4096);
  t.read("octaves", c.noise_octaves, 1, 6);
  auto ds = r.child("districts");
  ds.read("seeds", c.district_seeds, 4, 64);
  auto w = ds.child("weights");
  w.read("downtown", c.district_weights[0], 0, 1000);
  w.read("residential", c.district_weights[1], 0, 1000);
  w.read("park", c.district_weights[2], 0, 1000);
  w.read("industrial", c.district_weights[3], 0, 1000);
  auto z = r.child("pvp_zones");
  const char* names[] = {"downtown", "residential", "park", "industrial"};
  for (int i = 0; i < kDistrictCount; ++i) {
    std::string s = to_string(c.zone_rules[static_cast<size_t>(i)]);
    z.read(names[i], s);
    c.zone_rules[static_cast<size_t>(i)] = parse_zone(s, d);
  }
  auto rd = r.child("roads");
  rd.read("margin_m", c.margin_m, 4, 200);
  rd.read("arterial_spacing_m", c.arterial_spacing_m, 64, 2000);
  rd.read("arterial_jitter_m", c.arterial_jitter_m, 0, 40);
  rd.read_range("local_spacing_m", c.local_spacing_min_m, c.local_spacing_max_m, 24, 400);
  rd.read("arterial_half_width_cm", c.arterial_half_width_cm, 200, 1500);
  rd.read("local_half_width_cm", c.local_half_width_cm, 150, 1000);
  rd.read("path_half_width_cm", c.path_half_width_cm, 50, 500);
  rd.read("sidewalk_cm", c.sidewalk_cm, 0, 600);
  auto b = r.child("buildings");
  b.read("setback_front_cm", c.setback_front_cm, 0, 2000);
  b.read("setback_side_cm", c.setback_side_cm, 0, 1000);
  b.read("empty_lot_percent_residential", c.empty_lot_percent_residential, 0, 100);
  auto v = r.child("vegetation");
  v.read("park_tree_spacing_m", c.park_tree_spacing_m, 2, 100);
  v.read("lamp_spacing_m", c.lamp_spacing_m, 8, 200);
  v.read("street_tree_spacing_m", c.street_tree_spacing_m, 4, 200);
  auto p = r.child("pois");
  p.read_range("water_per_district", c.water_min, c.water_max, 0, 50);
  p.read_range("food_per_district", c.food_min, c.food_max, 0, 50);
  p.read_range("shelters", c.shelter_min, c.shelter_max, 0, 50);
  p.read("spawns", c.spawn_count, 1, 64);
  if (c.local_spacing_min_m * 2 > c.arterial_spacing_m)
    d.warnings.emplace_back("local_spacing_m lớn so với arterial_spacing_m: ít đường nhánh");
  if (c.chunk_size_m % c.terrain_cell_m != 0) d.errors.emplace_back("chunk_size_m phải chia hết cho terrain.cell_m");

  c.asset_rules = default_asset_rules();
  if (j.contains("asset_rules") && j["asset_rules"].is_object()) {
    for (const auto& [cat, list] : j["asset_rules"].items()) {
      std::vector<WeightedAsset> v2;
      if (!list.is_array()) {
        d.errors.push_back("asset_rules." + cat + " phải là mảng");
        continue;
      }
      for (const auto& e : list) {
        if (!e.is_object() || !e.contains("id") || !e["id"].is_string()) {
          d.errors.push_back("asset_rules." + cat + ": mỗi mục cần 'id'");
          continue;
        }
        v2.push_back({e["id"].get<std::string>(), e.value("weight", 1)});
      }
      c.asset_rules[cat] = v2;
    }
  }
  return c;
}
}  // namespace

WorldConfig parse_world_config(std::string_view text, ConfigDiagnostics& d) {
  auto j = parse_json_config(text, d);
  return from_json(j, d);
}

WorldConfig load_world_config(const std::filesystem::path& path, ConfigDiagnostics& d) {
  auto j = load_json_config(path, d, /*required=*/true);
  return from_json(j, d);
}

std::string world_config_hash(const WorldConfig& c) {
  // Chuẩn hóa: chuỗi key=value theo thứ tự cố định (không phụ thuộc định dạng file JSON).
  std::string s = std::format(
      "seed={};size={}x{};chunk={};tcell={};amp={:.4f};period={};oct={};dseeds={};dw={},{},{},{};zones={},{},{},{};"
      "margin={};art={};jit={};loc={}-{};hw={},{},{};sw={};sb={},{};empty={};tree={};lamp={};stree={};"
      "water={}-{};food={}-{};shelter={}-{};spawn={};",
      c.seed, c.size_m_x, c.size_m_z, c.chunk_size_m, c.terrain_cell_m, c.amplitude_m, c.noise_period_m,
      c.noise_octaves, c.district_seeds, c.district_weights[0], c.district_weights[1], c.district_weights[2],
      c.district_weights[3], static_cast<int>(c.zone_rules[0]), static_cast<int>(c.zone_rules[1]),
      static_cast<int>(c.zone_rules[2]), static_cast<int>(c.zone_rules[3]), c.margin_m, c.arterial_spacing_m,
      c.arterial_jitter_m, c.local_spacing_min_m, c.local_spacing_max_m, c.arterial_half_width_cm,
      c.local_half_width_cm, c.path_half_width_cm, c.sidewalk_cm, c.setback_front_cm, c.setback_side_cm,
      c.empty_lot_percent_residential, c.park_tree_spacing_m, c.lamp_spacing_m, c.street_tree_spacing_m, c.water_min,
      c.water_max, c.food_min, c.food_max, c.shelter_min, c.shelter_max, c.spawn_count);
  for (const auto& [cat, list] : c.asset_rules) {
    s += cat + ":";
    for (const auto& a : list) s += std::format("{}*{},", a.id, a.weight);
    s += ";";
  }
  return asset::sha256_hex({reinterpret_cast<const uint8_t*>(s.data()), s.size()});
}

}  // namespace pb::world
