#include <charconv>
#include <chrono>
#include <cstdio>
#include <format>
#include <string>

#include "tools/commands.hpp"
#include "world/generator.hpp"
#include "world/validator.hpp"
#include "world/world_config.hpp"
#include "world/world_io.hpp"

namespace pb::tools {
namespace {
using Args = std::vector<std::string_view>;
std::string_view opt(const Args& a, std::string_view name, std::string_view def = {}) {
  for (size_t i = 0; i + 1 < a.size(); ++i)
    if (a[i] == name) return a[i + 1];
  return def;
}
void print(const std::string& s) { std::fputs(s.c_str(), stdout); }

void print_stats(const world::WorldData& w) {
  const auto& s = w.stats;
  print(std::format("  kích thước {:.0f}x{:.0f} m, {}x{} = {} chunk ({:.0f} m)\n", world::to_m(w.size_x), world::to_m(w.size_z),
                    w.chunks_x, w.chunks_z, w.chunks_x * w.chunks_z, world::to_m(w.chunk_size)));
  print(std::format("  đường: {} nút, {} cạnh | block {} | lot {} | nhà {} | props {} | POI {} | nav {} nút | hồ {}\n",
                    s.road_nodes, s.road_edges, s.blocks, s.lots, s.buildings, s.props, s.pois, s.nav_nodes, s.ponds));
  int dc[world::kDistrictCount] = {};
  for (auto d : w.districts) ++dc[d];
  print(std::format("  district (ô 16 m): downtown {} | residential {} | park {} | industrial {}\n", dc[0], dc[1], dc[2], dc[3]));
  int pk[static_cast<int>(world::PoiKind::Count)] = {};
  for (const auto& c : w.chunks)
    for (const auto& p : c.pois) ++pk[static_cast<int>(p.kind)];
  print(std::format("  POI: nước {} | thức ăn {} | trú ẩn {} | cửa hàng {} | spawn {}\n", pk[0], pk[1], pk[2], pk[3], pk[4]));
}

int generate(const Args& a) {
  const auto cfg_path = opt(a, "--config", "data/world/demo.json");
  const auto out = opt(a, "--output");
  if (out.empty()) {
    std::fputs("cần --output <dir>\n", stderr);
    return 2;
  }
  ConfigDiagnostics diag;
  auto cfg = world::load_world_config(std::string(cfg_path), diag);
  for (const auto& w : diag.warnings) print(std::format("  cảnh báo config: {}\n", w));
  for (const auto& e : diag.errors) std::fprintf(stderr, "LỖI config: %s\n", e.c_str());
  if (!diag.ok()) return 2;
  if (auto s = opt(a, "--seed"); !s.empty()) {
    uint64_t v = 0;
    if (std::from_chars(s.data(), s.data() + s.size(), v).ec != std::errc{}) {
      std::fputs("--seed phải là số nguyên không âm\n", stderr);
      return 2;
    }
    cfg.seed = v;
  }
  const auto t0 = std::chrono::steady_clock::now();
  auto w = world::generate_world(cfg);
  w.stats.generate_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::string err;
  if (!world::save_world(std::string(out), w, err)) {
    std::fprintf(stderr, "LỖI: %s\n", err.c_str());
    return 2;
  }
  print(std::format("đã sinh thế giới seed {} -> {} trong {:.1f} ms\n", cfg.seed, std::string(out), w.stats.generate_ms));
  print_stats(w);
  print(std::format("  logical hash: {}\n", world::logical_hash(w)));
  return 0;
}

int validate(const Args& a) {
  if (a.empty()) return 2;
  const auto t0 = std::chrono::steady_clock::now();
  auto lr = world::load_world(std::string(a[0]));
  if (!lr.ok()) {
    std::fprintf(stderr, "LỖI: %s\n", lr.error.c_str());
    return 2;
  }
  world::ValidateOptions o;
  o.expected_hash = lr.stored_hash;
  const auto issues = world::validate_world(lr.world, o);
  for (const auto& i : issues) print(std::format("  [{}] {}\n", i.category, i.message));
  print(std::format("{}: {} lỗi ({:.1f} ms)\n", issues.empty() ? "HỢP LỆ" : "KHÔNG HỢP LỆ", issues.size(),
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()));
  return issues.empty() ? 0 : 1;
}

int inspect(const Args& a) {
  if (a.empty()) return 2;
  auto lr = world::load_world(std::string(a[0]));
  if (!lr.ok()) {
    std::fprintf(stderr, "LỖI: %s\n", lr.error.c_str());
    return 2;
  }
  print(std::format("world {} (format {}, generator {}, seed {})\n", std::string(a[0]), lr.world.format_version,
                    lr.world.generator_version, lr.world.seed));
  print_stats(lr.world);
  print(std::format("  logical hash (đã lưu): {}\n", lr.stored_hash));
  return 0;
}
}  // namespace

int world_command(std::string_view cmd, const std::vector<std::string_view>& args) {
  if (cmd == "generate") return generate(args);
  if (cmd == "validate") return validate(args);
  if (cmd == "inspect") return inspect(args);
  std::fprintf(stderr, "world: lệnh không hợp lệ\n");
  return 2;
}

}  // namespace pb::tools
