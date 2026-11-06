#pragma once
// Dữ liệu logic của thế giới. Tọa độ: số nguyên centimet (cm), mặt đất XZ, Y lên (ADR-007).
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pb::world {

using cm = int32_t;
inline constexpr uint32_t kWorldFormatVersion = 1;
inline constexpr uint32_t kGeneratorVersion = 1;

enum class District : uint8_t { Downtown = 0, Residential = 1, Park = 2, Industrial = 3 };
inline constexpr int kDistrictCount = 4;
enum class ZoneRule : uint8_t { Safe = 0, Restricted = 1, Contested = 2 };
enum class RoadClass : uint8_t { Arterial = 0, Local = 1, Path = 2 };
enum class PropKind : uint8_t { Tree, Pine, Bush, Bench, Lamp, TrashBin, Rock, Hydrant, Dumpster, Crate, Count };
enum class PoiKind : uint8_t { Water, Food, Shelter, Shop, Spawn, Count };

const char* to_string(District d);
const char* to_string(ZoneRule z);
const char* to_string(PropKind k);
const char* to_string(PoiKind k);

struct RoadNode {
  cm x = 0, z = 0;
};
struct RoadEdge {
  uint32_t a = 0, b = 0;
  RoadClass cls = RoadClass::Local;
  cm half_width = 0;  // nửa bề rộng lòng đường (hoặc lối đi)
  cm sidewalk = 0;    // bề rộng vỉa hè mỗi bên (0 với Path)
};

// Nhà: footprint đã xoay (half_x/half_z theo trục thế giới); rot = hướng mặt tiền (0:+Z,1:+X,2:-Z,3:-X).
struct Building {
  uint32_t id = 0;
  cm x = 0, z = 0;
  cm half_x = 0, half_z = 0;
  cm height = 0;
  uint8_t rot = 0;
  District district = District::Residential;
  uint16_t asset = 0;  // chỉ số vào WorldData::asset_ids
};

struct Prop {
  uint32_t id = 0;
  cm x = 0, z = 0;
  uint16_t yaw_deg = 0;
  PropKind kind = PropKind::Tree;
  uint8_t scale_pct = 100;
  uint16_t asset = 0;
};

struct Poi {
  uint32_t id = 0;
  cm x = 0, z = 0;
  PoiKind kind = PoiKind::Water;
  District district = District::Park;
};

struct Pond {
  cm x = 0, z = 0, radius = 0;
  cm level = 0;  // độ cao mặt nước (cm)
};

struct NavEdge {
  uint32_t a = 0, b = 0;
};

struct Terrain {
  int cells_x = 0, cells_z = 0;  // số ô; số đỉnh = (cells+1)^2
  cm cell = 400;
  std::vector<int16_t> height_cm;  // (cells_x+1)*(cells_z+1), hàng theo z
};

struct ChunkData {
  int cx = 0, cz = 0;
  std::vector<Building> buildings;
  std::vector<Prop> props;
  std::vector<Poi> pois;
};

struct WorldStats {
  uint32_t road_nodes = 0, road_edges = 0, blocks = 0, lots = 0, buildings = 0, props = 0, pois = 0, nav_nodes = 0;
  uint32_t ponds = 0;
  double generate_ms = 0;
};

struct WorldData {
  uint32_t format_version = kWorldFormatVersion;
  uint32_t generator_version = kGeneratorVersion;
  uint64_t seed = 0;
  std::string config_hash;
  cm size_x = 0, size_z = 0, chunk_size = 6400;
  int chunks_x = 0, chunks_z = 0;
  cm district_cell = 1600;
  int dcells_x = 0, dcells_z = 0;
  std::vector<uint8_t> districts;  // dcells_x * dcells_z
  std::array<ZoneRule, kDistrictCount> zone_rules{ZoneRule::Restricted, ZoneRule::Restricted, ZoneRule::Safe,
                                                  ZoneRule::Contested};
  Terrain terrain;
  std::vector<Pond> ponds;
  std::vector<RoadNode> road_nodes;
  std::vector<RoadEdge> road_edges;
  std::vector<RoadNode> nav_nodes;
  std::vector<NavEdge> nav_edges;
  std::vector<std::string> asset_ids;
  std::vector<ChunkData> chunks;  // chunks_x * chunks_z, chỉ số = cz * chunks_x + cx
  WorldStats stats;

  [[nodiscard]] int chunk_index(int cx, int cz) const { return cz * chunks_x + cx; }
  [[nodiscard]] District district_at(cm x, cm z) const;
  [[nodiscard]] ZoneRule zone_at(cm x, cm z) const { return zone_rules[static_cast<size_t>(district_at(x, z))]; }
  // Độ cao địa hình (m) nội suy song tuyến tại tọa độ (m).
  [[nodiscard]] float height_at(float x_m, float z_m) const;
  [[nodiscard]] bool in_pond(cm x, cm z, cm margin = 0) const;
};

// Đơn vị: chuyển cm <-> m.
inline constexpr float to_m(cm v) { return static_cast<float>(v) * 0.01f; }

}  // namespace pb::world
