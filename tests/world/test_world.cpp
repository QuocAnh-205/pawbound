#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <set>

#include "world/generator.hpp"
#include "world/rng.hpp"
#include "world/streaming.hpp"
#include "world/validator.hpp"
#include "world/world_config.hpp"
#include "world/world_io.hpp"

using namespace pb::world;
namespace fs = std::filesystem;

namespace {
WorldConfig demo_config() {
  pb::ConfigDiagnostics d;
  auto c = load_world_config(fs::path(PB_SOURCE_DIR) / "data/world/demo.json", d);
  REQUIRE(d.ok());
  return c;
}
bool has_issue(const std::vector<WorldIssue>& v, const std::string& cat) {
  for (const auto& i : v)
    if (i.category == cat) return true;
  return false;
}
}  // namespace

// Golden hash của seed demo. Nếu đổi thuật toán generator: PHẢI tăng kGeneratorVersion và cập nhật giá trị này.
constexpr const char* kDemoGoldenHash = "2733441ce1c7f768e5597ecb39a0f97e0ab55001dba7780ba9795c1a4219e607";

TEST_CASE("rng: PCG32 deterministic, range/below đúng biên") {
  Pcg32 a(42), b(42), c(43);
  for (int i = 0; i < 100; ++i) CHECK(a.next_u32() == b.next_u32());
  CHECK(Pcg32(42).next_u32() != c.next_u32());
  Pcg32 r(7);
  for (int i = 0; i < 10000; ++i) {
    const int v = r.range(-3, 3);
    CHECK(v >= -3);
    CHECK(v <= 3);
  }
  CHECK(sub_seed(1, "roads") != sub_seed(1, "terrain"));
  CHECK(sub_seed(1, "roads") == sub_seed(1, "roads"));
}

TEST_CASE("noise: fixed-point trong khoảng, liên tục, deterministic") {
  int32_t prev = value_noise_fixed(9, 0, 0, 1000);
  for (cm x = 1; x < 5000; x += 7) {
    const int32_t v = value_noise_fixed(9, x, 123, 1000);
    CHECK(v >= -32768);
    CHECK(v <= 32767);
    CHECK(std::abs(v - prev) < 2000);  // liên tục: bước nhỏ -> thay đổi nhỏ
    prev = v;
  }
  CHECK(fbm_fixed(5, -12345, 678, 16000, 3) == fbm_fixed(5, -12345, 678, 16000, 3));
}

TEST_CASE("world: cùng seed/config -> cùng logical hash; golden hash seed demo") {
  const auto cfg = demo_config();
  const auto w1 = generate_world(cfg);
  const auto w2 = generate_world(cfg);
  CHECK(logical_hash(w1) == logical_hash(w2));
  CHECK(logical_hash(w1) == kDemoGoldenHash);
  auto cfg2 = cfg;
  cfg2.seed = cfg.seed + 1;
  CHECK(logical_hash(generate_world(cfg2)) != logical_hash(w1));
  auto cfg3 = cfg;
  cfg3.lamp_spacing_m = 30;
  CHECK(logical_hash(generate_world(cfg3)) != logical_hash(w1));
  CHECK(world_config_hash(cfg) != world_config_hash(cfg3));
}

TEST_CASE("world: bản demo hợp lệ và có đủ thành phần thành phố") {
  const auto w = generate_world(demo_config());
  const auto issues = validate_world(w);
  for (const auto& i : issues) MESSAGE(i.category << ": " << i.message);
  CHECK(issues.empty());
  CHECK(w.stats.buildings > 50);
  CHECK(w.stats.props > 500);
  CHECK(w.ponds.size() >= 1);
  std::set<District> ds;
  for (auto d : w.districts) ds.insert(static_cast<District>(d));
  CHECK(ds.size() == 4);
  int spawns = 0, water = 0, food = 0;
  for (const auto& c : w.chunks)
    for (const auto& p : c.pois) {
      spawns += p.kind == PoiKind::Spawn;
      water += p.kind == PoiKind::Water;
      food += p.kind == PoiKind::Food;
    }
  CHECK(spawns == 4);
  CHECK(water >= 4);
  CHECK(food >= 4);
}

TEST_CASE("world: validator phát hiện lỗi cố ý (fixture lỗi)") {
  const auto base = generate_world(demo_config());
  REQUIRE(validate_world(base).empty());

  SUBCASE("nhà chồng lấn") {
    auto w = base;
    for (auto& c : w.chunks)
      if (!c.buildings.empty()) {
        auto copy = c.buildings[0];
        copy.id ^= 0xFFFF;
        copy.x += 10;
        c.buildings.push_back(copy);
        break;
      }
    CHECK(has_issue(validate_world(w), "building_overlap"));
  }
  SUBCASE("đường bị đứt (nút cô lập)") {
    auto w = base;
    w.road_nodes.push_back({100, 100});
    CHECK(has_issue(validate_world(w), "road_connectivity"));
  }
  SUBCASE("nhà đè lên đường") {
    auto w = base;
    const auto& e = w.road_edges[0];
    for (auto& c : w.chunks)
      if (!c.buildings.empty()) {
        c.buildings[0].x = (w.road_nodes[e.a].x + w.road_nodes[e.b].x) / 2;
        c.buildings[0].z = (w.road_nodes[e.a].z + w.road_nodes[e.b].z) / 2;
        break;
      }
    const auto iss = validate_world(w);
    CHECK((has_issue(iss, "building_road") || has_issue(iss, "chunk")));
  }
  SUBCASE("prop nằm trong nhà") {
    auto w = base;
    Building b{};
    for (auto& c : w.chunks)
      if (!c.buildings.empty()) {
        b = c.buildings[0];
        Prop p;
        p.id = 0x2FFFFFFF;
        p.x = b.x;
        p.z = b.z;
        p.kind = PropKind::Bench;
        c.props.push_back(p);
        break;
      }
    CHECK(has_issue(validate_world(w), "prop_obstacle"));
  }
  SUBCASE("spawn đặt trong nhà / ngoài vùng safe") {
    auto w = base;
    Building b{};
    for (auto& c : w.chunks)
      if (!c.buildings.empty()) b = c.buildings[0];
    for (auto& c : w.chunks)
      for (auto& p : c.pois)
        if (p.kind == PoiKind::Spawn) {
          p.x = b.x;
          p.z = b.z;
        }
    const auto iss = validate_world(w);
    CHECK((has_issue(iss, "spawn") || has_issue(iss, "poi_unreachable")));
  }
  SUBCASE("instance ghi sai chunk + trùng id") {
    auto w = base;
    auto& c0 = w.chunks[0];
    auto& c1 = w.chunks.back();
    REQUIRE(!c1.props.empty());
    c0.props.push_back(c1.props[0]);
    const auto iss = validate_world(w);
    CHECK(has_issue(iss, "chunk"));
  }
  SUBCASE("hash không khớp") {
    ValidateOptions o;
    o.expected_hash = std::string(64, 'a');
    CHECK(has_issue(validate_world(base, o), "hash"));
  }
}

TEST_CASE("world: lưu/nạp file giữ nguyên hash; file hỏng bị từ chối") {
  const auto w = generate_world(demo_config());
  const auto dir = fs::temp_directory_path() / "pawbound_tests" / "world_io";
  fs::remove_all(dir);
  std::string err;
  REQUIRE(save_world(dir, w, err));
  auto lr = load_world(dir);
  REQUIRE_MESSAGE(lr.ok(), lr.error);
  CHECK(logical_hash(lr.world) == logical_hash(w));
  CHECK(lr.stored_hash == logical_hash(w));
  ValidateOptions o;
  o.expected_hash = lr.stored_hash;
  CHECK(validate_world(lr.world, o).empty());

  // Cắt cụt world.bin
  auto bytes = serialize_global(w);
  bytes.resize(bytes.size() / 2);
  WorldData tmp;
  CHECK_FALSE(deserialize_global(bytes, tmp, err));
  // Sai magic
  auto bad = serialize_global(w);
  bad[0] = 'X';
  CHECK_FALSE(deserialize_global(bad, tmp, err));
  // Số phần tử khổng lồ (file độc hại) không được cấp phát vô hạn
  auto chunk = serialize_chunk(w.chunks[0]);
  chunk[16] = 0xFF;
  chunk[17] = 0xFF;
  chunk[18] = 0xFF;
  chunk[19] = 0x7F;
  ChunkData cd;
  CHECK_FALSE(deserialize_chunk(chunk, cd, err));
  // File chunk bị đổi tên (tọa độ không khớp)
  fs::copy_file(dir / "chunks" / "0_0.bin", dir / "chunks" / "1_0.bin", fs::copy_options::overwrite_existing);
  CHECK_FALSE(load_chunk(dir, 1, 0, cd, err));
}

TEST_CASE("streaming: load/unload theo vùng, có hysteresis, không trùng") {
  ChunkStreamer s(8, 8, 64.0f, 1, 2);
  auto u = s.update(32, 32);  // chunk (0,0)
  CHECK(u.load.size() == 4);  // (0..1)x(0..1)
  CHECK(u.unload.empty());
  u = s.update(40, 40);  // vẫn chunk (0,0): không có gì mới
  CHECK(u.load.empty());
  CHECK(u.unload.empty());
  u = s.update(64 * 3 + 5, 32);  // sang chunk (3,0)
  std::set<int> loads(u.load.begin(), u.load.end());
  CHECK(loads.size() == u.load.size());  // không trùng trong một lần
  for (int idx : u.load) CHECK(s.loaded().count(idx) == 1);
  // Chunk (0,0) cách 3 > unload_radius 2 -> bị gỡ; chunk (1,0) cách 2 -> giữ (hysteresis)
  CHECK(std::find(u.unload.begin(), u.unload.end(), 0) != u.unload.end());
  CHECK(s.loaded().count(1) == 1);
  // Đi tới đi lui ở mép chunk không gây load/unload liên tục
  int churn = 0;
  for (int i = 0; i < 20; ++i) {
    auto a = s.update(i % 2 ? 64 * 3 - 1.0f : 64 * 3 + 1.0f, 32);
    churn += static_cast<int>(a.load.size() + a.unload.size());
  }
  CHECK(churn <= 6);
  for (int idx : s.loaded()) CHECK(idx >= 0);
}
