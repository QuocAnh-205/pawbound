#include "world/generator.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>

#include "world/geometry2d.hpp"
#include "world/rng.hpp"

namespace pb::world {

// ============================================================================ noise (số nguyên)
namespace {
inline int64_t floor_div(int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

inline int32_t lattice(uint64_t seed, int64_t ix, int64_t iz) {
  const uint64_t k = (static_cast<uint64_t>(static_cast<uint32_t>(ix)) << 32) | static_cast<uint32_t>(iz);
  return static_cast<int32_t>(splitmix64(seed ^ splitmix64(k)) >> 48) - 32768;
}
// smoothstep trong Q16
inline int64_t smooth_q16(int64_t t) {
  const int64_t t2 = (t * t) >> 16;
  return (t2 * (3 * 65536 - 2 * t)) >> 16;
}
}  // namespace

int32_t value_noise_fixed(uint64_t seed, cm x, cm z, cm period) {
  const int64_t ix = floor_div(x, period), iz = floor_div(z, period);
  const int64_t fx = ((static_cast<int64_t>(x) - ix * period) << 16) / period;
  const int64_t fz = ((static_cast<int64_t>(z) - iz * period) << 16) / period;
  const int64_t sx = smooth_q16(fx), sz = smooth_q16(fz);
  const int64_t a = lattice(seed, ix, iz), b = lattice(seed, ix + 1, iz);
  const int64_t c = lattice(seed, ix, iz + 1), d = lattice(seed, ix + 1, iz + 1);
  const int64_t ab = a + (((b - a) * sx) >> 16);
  const int64_t cd = c + (((d - c) * sx) >> 16);
  return static_cast<int32_t>(ab + (((cd - ab) * sz) >> 16));
}

int32_t fbm_fixed(uint64_t seed, cm x, cm z, cm period, int octaves) {
  int64_t sum = 0, norm = 0, amp = 1 << 12;
  for (int o = 0; o < octaves; ++o) {
    sum += value_noise_fixed(splitmix64(seed + static_cast<uint64_t>(o)), x, z, std::max<cm>(period >> o, 1)) * amp;
    norm += amp;
    amp >>= 1;
  }
  return static_cast<int32_t>(sum / norm);
}

// ============================================================================ helpers
const char* to_string(District d) {
  switch (d) {
    case District::Downtown: return "downtown";
    case District::Residential: return "residential";
    case District::Park: return "park";
    case District::Industrial: return "industrial";
  }
  return "?";
}
const char* to_string(ZoneRule z) {
  switch (z) {
    case ZoneRule::Safe: return "safe";
    case ZoneRule::Restricted: return "restricted";
    case ZoneRule::Contested: return "contested";
  }
  return "?";
}
const char* to_string(PropKind k) {
  static const char* n[] = {"tree", "pine", "bush", "bench", "lamp", "trash_bin", "rock", "hydrant", "dumpster", "crate"};
  const auto i = static_cast<size_t>(k);
  return i < std::size(n) ? n[i] : "?";
}
const char* to_string(PoiKind k) {
  static const char* n[] = {"water", "food", "shelter", "shop", "spawn"};
  const auto i = static_cast<size_t>(k);
  return i < std::size(n) ? n[i] : "?";
}

District WorldData::district_at(cm x, cm z) const {
  if (districts.empty()) return District::Residential;
  const int cx = std::clamp(static_cast<int>(x / district_cell), 0, dcells_x - 1);
  const int cz = std::clamp(static_cast<int>(z / district_cell), 0, dcells_z - 1);
  return static_cast<District>(districts[static_cast<size_t>(cz * dcells_x + cx)]);
}

float WorldData::height_at(float x_m, float z_m) const {
  const auto& t = terrain;
  if (t.height_cm.empty()) return 0.0f;
  const float cell = to_m(t.cell);
  const float fx = std::clamp(x_m / cell, 0.0f, static_cast<float>(t.cells_x) - 1e-4f);
  const float fz = std::clamp(z_m / cell, 0.0f, static_cast<float>(t.cells_z) - 1e-4f);
  const int ix = static_cast<int>(fx), iz = static_cast<int>(fz);
  const float ux = fx - static_cast<float>(ix), uz = fz - static_cast<float>(iz);
  const int row = t.cells_x + 1;
  auto h = [&](int x, int z) { return static_cast<float>(t.height_cm[static_cast<size_t>(z * row + x)]) * 0.01f; };
  const float a = h(ix, iz) + (h(ix + 1, iz) - h(ix, iz)) * ux;
  const float b = h(ix, iz + 1) + (h(ix + 1, iz + 1) - h(ix, iz + 1)) * ux;
  return a + (b - a) * uz;
}

bool WorldData::in_pond(cm x, cm z, cm margin) const {
  for (const auto& p : ponds)
    if (dist2(x, z, p.x, p.z) < static_cast<int64_t>(p.radius + margin) * (p.radius + margin)) return true;
  return false;
}

namespace {
struct Seg {
  bool vertical;  // true: x = fixed, z chạy [lo, hi]
  cm fixed, lo, hi;
  RoadClass cls;
};

// Lưới chiếm chỗ cho props (khoảng cách tối thiểu).
class PointGrid {
 public:
  explicit PointGrid(cm cell) : cell_(cell) {}
  bool free(cm x, cm z, cm min_dist) const {
    const int64_t r = min_dist / cell_ + 1;
    const int64_t gx = x / cell_, gz = z / cell_;
    for (int64_t dz = -r; dz <= r; ++dz)
      for (int64_t dx = -r; dx <= r; ++dx) {
        auto it = cells_.find(key(gx + dx, gz + dz));
        if (it == cells_.end()) continue;
        for (const auto& p : it->second)
          if (dist2(x, z, p.first, p.second) < static_cast<int64_t>(min_dist) * min_dist) return false;
      }
    return true;
  }
  void add(cm x, cm z) { cells_[key(x / cell_, z / cell_)].emplace_back(x, z); }

 private:
  static uint64_t key(int64_t gx, int64_t gz) { return (static_cast<uint64_t>(gx + 1000000) << 32) | static_cast<uint64_t>(gz + 1000000); }
  cm cell_;
  std::unordered_map<uint64_t, std::vector<std::pair<cm, cm>>> cells_;
};

struct Gen {
  const WorldConfig& cfg;
  WorldData w;
  std::vector<Seg> segs;
  std::vector<Rect> carriageways;  // lòng đường (không gồm vỉa hè)
  std::vector<Rect> road_full;     // gồm vỉa hè
  std::vector<std::pair<RoadNode, RoadNode>> paths;
  std::vector<Rect> building_rects;
  std::vector<Building> buildings;
  std::vector<Prop> props;
  std::vector<Poi> pois;
  std::vector<std::pair<cm, cm>> plazas;  // tâm quảng trường công viên
  struct ArterialCell {
    Rect r;
    District d;
    std::vector<cm> xs, zs;  // đường nhánh bên trong ô
  };
  std::vector<ArterialCell> cells;
  PointGrid prop_grid{400};
  std::map<std::string, uint16_t> asset_index;

  explicit Gen(const WorldConfig& c) : cfg(c) {}

  cm half_width(RoadClass c) const {
    return c == RoadClass::Arterial ? cfg.arterial_half_width_cm
           : c == RoadClass::Local  ? cfg.local_half_width_cm
                                    : cfg.path_half_width_cm;
  }
  cm sidewalk(RoadClass c) const { return c == RoadClass::Path ? 0 : cfg.sidewalk_cm; }

  uint16_t pick_asset(const std::string& category, Pcg32& rng) {
    auto it = cfg.asset_rules.find(category);
    std::string id = "placeholder:" + category;
    if (it != cfg.asset_rules.end() && !it->second.empty()) {
      int total = 0;
      for (const auto& a : it->second) total += std::max(a.weight, 0);
      int r = total > 0 ? static_cast<int>(rng.below(static_cast<uint32_t>(total))) : 0;
      for (const auto& a : it->second) {
        r -= std::max(a.weight, 0);
        if (r < 0) {
          id = a.id;
          break;
        }
      }
    }
    auto [pos, inserted] = asset_index.try_emplace(id, static_cast<uint16_t>(w.asset_ids.size()));
    if (inserted) w.asset_ids.push_back(id);
    return pos->second;
  }

  // ---------------------------------------------------------------- 1. district
  void districts() {
    Pcg32 rng(sub_seed(cfg.seed, "districts"));
    const int k = cfg.district_seeds;
    std::vector<std::pair<cm, cm>> seeds;
    for (int i = 0; i < k; ++i)
      seeds.emplace_back(rng.range(0, w.size_x), rng.range(0, w.size_z));
    const cm cx = w.size_x / 2, cz = w.size_z / 2;
    std::vector<int> order(static_cast<size_t>(k));
    for (int i = 0; i < k; ++i) order[static_cast<size_t>(i)] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
      return dist2(seeds[static_cast<size_t>(a)].first, seeds[static_cast<size_t>(a)].second, cx, cz) <
             dist2(seeds[static_cast<size_t>(b)].first, seeds[static_cast<size_t>(b)].second, cx, cz);
    });
    std::vector<District> type(static_cast<size_t>(k), District::Residential);
    // Ràng buộc: gần tâm = trung tâm; xa nhất = công nghiệp; bảo đảm có công viên + dân cư.
    type[static_cast<size_t>(order[0])] = District::Downtown;
    type[static_cast<size_t>(order[static_cast<size_t>(k - 1)])] = District::Industrial;
    type[static_cast<size_t>(order[static_cast<size_t>(k / 2)])] = District::Park;
    type[static_cast<size_t>(order[1])] = District::Residential;
    const int total = cfg.district_weights[0] + cfg.district_weights[1] + cfg.district_weights[2] + cfg.district_weights[3];
    for (int i = 2; i < k - 1; ++i) {
      if (i == k / 2) continue;
      int r = total > 0 ? static_cast<int>(rng.below(static_cast<uint32_t>(total))) : 1;
      int t = 0;
      while (t < 3 && r >= cfg.district_weights[static_cast<size_t>(t)]) r -= cfg.district_weights[static_cast<size_t>(t++)];
      type[static_cast<size_t>(order[static_cast<size_t>(i)])] = static_cast<District>(t);
    }
    w.district_cell = 1600;
    w.dcells_x = (w.size_x + w.district_cell - 1) / w.district_cell;
    w.dcells_z = (w.size_z + w.district_cell - 1) / w.district_cell;
    w.districts.resize(static_cast<size_t>(w.dcells_x * w.dcells_z));
    for (int z = 0; z < w.dcells_z; ++z)
      for (int x = 0; x < w.dcells_x; ++x) {
        const cm px = x * w.district_cell + w.district_cell / 2, pz = z * w.district_cell + w.district_cell / 2;
        int best = 0;
        int64_t bd = INT64_MAX;
        for (int i = 0; i < k; ++i) {
          const int64_t d = dist2(px, pz, seeds[static_cast<size_t>(i)].first, seeds[static_cast<size_t>(i)].second);
          if (d < bd) {
            bd = d;
            best = i;
          }
        }
        w.districts[static_cast<size_t>(z * w.dcells_x + x)] = static_cast<uint8_t>(type[static_cast<size_t>(best)]);
      }
  }

  // ---------------------------------------------------------------- 2. terrain
  void terrain() {
    auto& t = w.terrain;
    t.cell = cfg.terrain_cell_m * 100;
    t.cells_x = w.size_x / t.cell;
    t.cells_z = w.size_z / t.cell;
    t.height_cm.resize(static_cast<size_t>((t.cells_x + 1) * (t.cells_z + 1)));
    const uint64_t s = sub_seed(cfg.seed, "terrain");
    const auto amp = static_cast<int64_t>(std::llround(cfg.amplitude_m * 100.0));
    for (int z = 0; z <= t.cells_z; ++z)
      for (int x = 0; x <= t.cells_x; ++x) {
        const int32_t n = fbm_fixed(s, x * t.cell, z * t.cell, cfg.noise_period_m * 100, cfg.noise_octaves);
        t.height_cm[static_cast<size_t>(z * (t.cells_x + 1) + x)] = static_cast<int16_t>(amp * n / 32768);
      }
  }
  int16_t& hv(int x, int z) { return w.terrain.height_cm[static_cast<size_t>(z * (w.terrain.cells_x + 1) + x)]; }
  cm height_cm_at(cm x, cm z) const { return static_cast<cm>(std::lround(w.height_at(to_m(x), to_m(z)) * 100.0f)); }

  // ---------------------------------------------------------------- 3. roads
  std::vector<cm> arterial_lines(cm size, Pcg32& rng) const {
    const cm m = cfg.margin_m * 100;
    const cm inner = size - 2 * m;
    const int n = std::max(1, static_cast<int>(std::lround(static_cast<double>(inner) / (cfg.arterial_spacing_m * 100))));
    std::vector<cm> v{m};
    for (int i = 1; i < n; ++i) {
      const cm base = m + inner * i / n;
      v.push_back(base + rng.range(-cfg.arterial_jitter_m * 100, cfg.arterial_jitter_m * 100));
    }
    v.push_back(size - m);
    return v;
  }

  void roads() {
    Pcg32 rng(sub_seed(cfg.seed, "roads"));
    const auto xs = arterial_lines(w.size_x, rng);
    const auto zs = arterial_lines(w.size_z, rng);
    for (cm x : xs) segs.push_back({true, x, zs.front(), zs.back(), RoadClass::Arterial});
    for (cm z : zs) segs.push_back({false, z, xs.front(), xs.back(), RoadClass::Arterial});

    std::vector<std::pair<cm, cm>> extra_points;  // điểm nối lối đi công viên vào đường
    for (size_t i = 0; i + 1 < xs.size(); ++i) {
      for (size_t j = 0; j + 1 < zs.size(); ++j) {
        const Rect cell{xs[i], zs[j], xs[i + 1], zs[j + 1]};
        Pcg32 crng(sub_seed(cfg.seed, "cell", i, j));
        const District d = w.district_at(cell.cx(), cell.cz());
        if (d == District::Park) {
          // Quảng trường lệch tâm + 4 lối đi tới trung điểm 4 cạnh.
          const cm px = cell.cx() + crng.range(-cell.w() / 10, cell.w() / 10);
          const cm pz = cell.cz() + crng.range(-cell.d() / 10, cell.d() / 10);
          plazas.emplace_back(px, pz);
          const std::pair<cm, cm> mids[] = {{cell.x0, pz}, {cell.x1, pz}, {px, cell.z0}, {px, cell.z1}};
          for (const auto& mp : mids) {
            extra_points.push_back(mp);
            paths.push_back({{mp.first, mp.second}, {px, pz}});
          }
          // Hồ ở một góc phần tư (tránh lối đi).
          const int q = static_cast<int>(crng.below(4));
          const cm qx = cell.cx() + ((q & 1) ? 1 : -1) * cell.w() / 4;
          const cm qz = cell.cz() + ((q & 2) ? 1 : -1) * cell.d() / 4;
          const cm r = crng.range(700, 1300);
          const cm max_r = std::min(cell.w(), cell.d()) / 4 - 600;
          if (max_r > 500) w.ponds.push_back({qx, qz, std::min(r, max_r), 0});
          continue;
        }
        auto locals = [&](cm lo, cm hi) {
          std::vector<cm> v;
          cm p = lo;
          for (;;) {
            p += crng.range(cfg.local_spacing_min_m * 100, cfg.local_spacing_max_m * 100);
            if (p > hi - cfg.local_spacing_min_m * 100 / 2) break;
            v.push_back(p);
          }
          return v;
        };
        ArterialCell ac{cell, d, locals(cell.x0, cell.x1), locals(cell.z0, cell.z1)};
        for (cm x : ac.xs) segs.push_back({true, x, cell.z0, cell.z1, RoadClass::Local});
        for (cm z : ac.zs) segs.push_back({false, z, cell.x0, cell.x1, RoadClass::Local});
        cells.push_back(std::move(ac));
      }
    }

    // Đồ thị: nút tại giao điểm + đầu mút + điểm phụ; cạnh nối các nút liên tiếp trên mỗi đoạn.
    std::map<std::pair<cm, cm>, uint32_t> node_id;
    auto node = [&](cm x, cm z) {
      auto [it, ins] = node_id.try_emplace({x, z}, static_cast<uint32_t>(w.road_nodes.size()));
      if (ins) w.road_nodes.push_back({x, z});
      return it->second;
    };
    std::set<std::pair<uint32_t, uint32_t>> edge_set;
    for (const auto& s : segs) {
      std::vector<cm> pts{s.lo, s.hi};
      for (const auto& o : segs) {
        if (o.vertical == s.vertical) continue;
        if (o.fixed >= s.lo && o.fixed <= s.hi && s.fixed >= o.lo && s.fixed <= o.hi) pts.push_back(o.fixed);
      }
      for (const auto& e : extra_points) {
        const cm along = s.vertical ? e.second : e.first;
        const cm across = s.vertical ? e.first : e.second;
        if (across == s.fixed && along >= s.lo && along <= s.hi) pts.push_back(along);
      }
      std::sort(pts.begin(), pts.end());
      pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
      for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const uint32_t a = s.vertical ? node(s.fixed, pts[i]) : node(pts[i], s.fixed);
        const uint32_t b = s.vertical ? node(s.fixed, pts[i + 1]) : node(pts[i + 1], s.fixed);
        if (!edge_set.insert({std::min(a, b), std::max(a, b)}).second) continue;
        w.road_edges.push_back({a, b, s.cls, half_width(s.cls), sidewalk(s.cls)});
      }
    }
    for (const auto& p : paths) {
      const uint32_t a = node(p.first.x, p.first.z), b = node(p.second.x, p.second.z);
      if (a == b || !edge_set.insert({std::min(a, b), std::max(a, b)}).second) continue;
      w.road_edges.push_back({a, b, RoadClass::Path, half_width(RoadClass::Path), 0});
    }
    for (const auto& e : w.road_edges) {
      if (e.cls == RoadClass::Path) continue;
      carriageways.push_back(road_rect(w.road_nodes[e.a], w.road_nodes[e.b], e.half_width));
      road_full.push_back(road_rect(w.road_nodes[e.a], w.road_nodes[e.b], e.half_width + e.sidewalk));
    }
    // Hồ: hạ địa hình, đặt mực nước.
    for (auto& p : w.ponds) {
      const cm base = height_cm_at(p.x, p.z);
      p.level = base - 30;
      const auto& t = w.terrain;
      for (int z = 0; z <= t.cells_z; ++z)
        for (int x = 0; x <= t.cells_x; ++x) {
          const int64_t d2 = dist2(x * t.cell, z * t.cell, p.x, p.z);
          const int64_t r2 = static_cast<int64_t>(p.radius + 200) * (p.radius + 200);
          if (d2 >= r2) continue;
          const double f = 1.0 - static_cast<double>(d2) / static_cast<double>(r2);
          const auto depth = static_cast<int16_t>(std::lround(160.0 * f));
          hv(x, z) = std::min<int16_t>(hv(x, z), static_cast<int16_t>(base - depth));
        }
    }
  }

  bool blocked_for_building(const Rect& r) const {
    for (const auto& rr : road_full)
      if (rr.overlaps(r)) return true;
    for (const auto& br : building_rects)
      if (br.overlaps(r)) return true;
    for (const auto& p : w.ponds)
      if (point_rect_dist2(p.x, p.z, r) < static_cast<int64_t>(p.radius + 200) * (p.radius + 200)) return true;
    for (const auto& p : paths)
      if (segment_hits_rect(p.first.x, p.first.z, p.second.x, p.second.z, r.inset(-cfg.path_half_width_cm))) return true;
    return false;
  }

  // ---------------------------------------------------------------- 4-6. block, lot, nhà
  void blocks_and_buildings() {
    // Block = hình chữ nhật giữa các đường của CHÍNH ô đường chính đó (không dùng đường của ô khác).
    const cm art = cfg.arterial_half_width_cm + cfg.sidewalk_cm;
    const cm loc = cfg.local_half_width_cm + cfg.sidewalk_cm;
    Pcg32 rng(sub_seed(cfg.seed, "buildings"));
    for (const auto& c : cells) {
      std::vector<std::pair<cm, cm>> xs{{c.r.x0, art}}, zs{{c.r.z0, art}};  // (vị trí, nửa bề rộng)
      for (cm x : c.xs) xs.emplace_back(x, loc);
      for (cm z : c.zs) zs.emplace_back(z, loc);
      xs.emplace_back(c.r.x1, art);
      zs.emplace_back(c.r.z1, art);
      for (size_t i = 0; i + 1 < xs.size(); ++i)
        for (size_t j = 0; j + 1 < zs.size(); ++j) {
          const Rect blk{xs[i].first + xs[i].second, zs[j].first + zs[j].second, xs[i + 1].first - xs[i + 1].second,
                         zs[j + 1].first - zs[j + 1].second};
          if (!blk.valid() || blk.w() < 800 || blk.d() < 800) continue;
          ++w.stats.blocks;
          lots_in_block(blk, w.district_at(blk.cx(), blk.cz()) == District::Park ? District::Residential
                                                                                 : w.district_at(blk.cx(), blk.cz()),
                        rng);
        }
    }
  }

  struct LotRange {
    cm lo, hi;
  };
  LotRange lot_width(District d) const {
    switch (d) {
      case District::Downtown: return {1400, 2400};
      case District::Industrial: return {2000, 3600};
      default: return {1000, 1600};
    }
  }

  void lots_in_block(const Rect& blk, District d, Pcg32& rng) {
    const bool long_x = blk.w() >= blk.d();
    const cm length = long_x ? blk.w() : blk.d();
    const cm depth = long_x ? blk.d() : blk.w();
    const int rows = depth >= 2800 ? 2 : 1;
    const auto lw = lot_width(d);
    for (int row = 0; row < rows; ++row) {
      // Lot dọc theo trục dài; hàng 0 nhìn về phía "thấp" (z0 hoặc x0), hàng 1 về phía "cao".
      cm pos = 0;
      while (length - pos >= lw.lo) {
        cm wdt = rng.range(lw.lo, lw.hi);
        if (length - pos - wdt < lw.lo) wdt = length - pos;
        Rect lot;
        const cm d0 = rows == 1 ? 0 : (row == 0 ? 0 : depth / 2);
        const cm d1 = rows == 1 ? depth : (row == 0 ? depth / 2 : depth);
        if (long_x) lot = {blk.x0 + pos, blk.z0 + d0, blk.x0 + pos + wdt, blk.z0 + d1};
        else lot = {blk.x0 + d0, blk.z0 + pos, blk.x0 + d1, blk.z0 + pos + wdt};
        pos += wdt;
        ++w.stats.lots;
        const bool face_high = rows == 2 ? row == 1 : rng.chance(50);
        // rot: 0:+Z 1:+X 2:-Z 3:-X
        const uint8_t rot = long_x ? (face_high ? 0 : 2) : (face_high ? 1 : 3);
        place_building(lot, d, rot, rng);
      }
    }
  }

  void place_building(const Rect& lot, District d, uint8_t rot, Pcg32& rng) {
    if (d == District::Residential && rng.chance(static_cast<uint32_t>(cfg.empty_lot_percent_residential))) {
      // Sân trống: một cây
      add_prop(lot.cx(), lot.cz(), PropKind::Tree, rng, 300, false);
      return;
    }
    Rect f = lot.inset(cfg.setback_side_cm);
    const cm front = cfg.setback_front_cm - cfg.setback_side_cm;
    switch (rot) {  // lùi mặt tiền thêm
      case 0: f.z1 -= front; break;
      case 1: f.x1 -= front; break;
      case 2: f.z0 += front; break;
      default: f.x0 += front; break;
    }
    if (!f.valid() || f.w() < 500 || f.d() < 500) return;
    // Nhà nhỏ hơn lot một chút để có biến thể.
    const cm shrink_x = d == District::Residential ? rng.range(0, f.w() / 6) : 0;
    const cm shrink_z = d == District::Residential ? rng.range(0, f.d() / 6) : 0;
    f.x0 += shrink_x / 2;
    f.x1 -= shrink_x / 2;
    f.z0 += shrink_z / 2;
    f.z1 -= shrink_z / 2;
    if (blocked_for_building(f)) return;
    Building b;
    b.x = f.cx();
    b.z = f.cz();
    b.half_x = f.w() / 2;
    b.half_z = f.d() / 2;
    b.rot = rot;
    b.district = d;
    switch (d) {
      case District::Downtown: b.height = rng.range(1200, 4200); break;
      case District::Industrial: b.height = rng.range(600, 1200); break;
      default: b.height = rng.range(400, 900); break;
    }
    b.asset = pick_asset(std::string("building.") + to_string(d), rng);
    buildings.push_back(b);
    building_rects.push_back(building_rect(b));
    // Industrial: thùng rác lớn trước mặt tiền
    if (d == District::Industrial && rng.chance(60)) {
      const auto fp = front_point(b, 150);
      add_prop(fp.first + rng.range(-b.half_x / 2, b.half_x / 2) * (rot % 2 == 0), fp.second + rng.range(-b.half_z / 2, b.half_z / 2) * (rot % 2 == 1),
               PropKind::Dumpster, rng, 200, false);
    }
  }

  // Điểm cách mặt tiền nhà `dist` cm.
  static std::pair<cm, cm> front_point(const Building& b, cm dist) {
    switch (b.rot) {
      case 0: return {b.x, b.z + b.half_z + dist};
      case 1: return {b.x + b.half_x + dist, b.z};
      case 2: return {b.x, b.z - b.half_z - dist};
      default: return {b.x - b.half_x - dist, b.z};
    }
  }

  // ---------------------------------------------------------------- 7. props
  bool prop_ok(cm x, cm z, cm radius, bool on_sidewalk) const {
    if (x < radius || z < radius || x > w.size_x - radius || z > w.size_z - radius) return false;
    if (w.in_pond(x, z, radius + 100)) return false;
    for (const auto& r : building_rects)
      if (point_rect_dist2(x, z, r) < static_cast<int64_t>(radius) * radius) return false;
    const auto& rects = on_sidewalk ? carriageways : road_full;
    for (const auto& r : rects)
      if (point_rect_dist2(x, z, r) < static_cast<int64_t>(radius) * radius) return false;
    for (const auto& p : paths) {
      const double lim = static_cast<double>(cfg.path_half_width_cm + radius);
      if (point_segment_dist2(x, z, p.first.x, p.first.z, p.second.x, p.second.z) < lim * lim) return false;
    }
    return true;
  }

  bool add_prop(cm x, cm z, PropKind k, Pcg32& rng, cm min_dist, bool on_sidewalk) {
    const cm radius = k == PropKind::Tree || k == PropKind::Pine ? 60 : k == PropKind::Dumpster ? 120 : 40;
    if (!prop_ok(x, z, radius, on_sidewalk) || !prop_grid.free(x, z, min_dist)) return false;
    Prop p;
    p.x = x;
    p.z = z;
    p.kind = k;
    p.yaw_deg = static_cast<uint16_t>(rng.below(360));
    p.scale_pct = static_cast<uint8_t>(rng.range(85, 120));
    p.asset = pick_asset(k == PropKind::Tree || k == PropKind::Pine ? "tree" : "prop", rng);
    props.push_back(p);
    prop_grid.add(x, z);
    return true;
  }

  void street_props() {
    Pcg32 rng(sub_seed(cfg.seed, "street_props"));
    for (const auto& e : w.road_edges) {
      if (e.cls == RoadClass::Path) continue;
      const auto& A = w.road_nodes[e.a];
      const auto& B = w.road_nodes[e.b];
      const cm len = std::abs(B.x - A.x) + std::abs(B.z - A.z);
      const int dx = (B.x > A.x) - (B.x < A.x), dz = (B.z > A.z) - (B.z < A.z);
      const cm off = e.half_width + e.sidewalk / 2;
      const District dmid = w.district_at((A.x + B.x) / 2, (A.z + B.z) / 2);
      int side = 1;
      for (cm s = 800; s <= len - 800; s += cfg.lamp_spacing_m * 100) {
        const cm x = A.x + dx * s - dz * off * side, z = A.z + dz * s + dx * off * side;
        add_prop(x, z, PropKind::Lamp, rng, 600, true);
        if (rng.chance(25) && dmid != District::Industrial) add_prop(x + dx * 150, z + dz * 150, PropKind::TrashBin, rng, 100, true);
        side = -side;
      }
      if (dmid == District::Residential || dmid == District::Park || dmid == District::Downtown) {
        for (cm s = 600; s <= len - 600; s += cfg.street_tree_spacing_m * 100) {
          const cm x = A.x + dx * s + dz * off, z = A.z + dz * s - dx * off;
          add_prop(x, z, PropKind::Tree, rng, 500, true);
        }
      }
      if (rng.chance(20)) {
        const cm s = rng.range(600, std::max<cm>(601, len - 600));
        add_prop(A.x + dx * s - dz * off, A.z + dz * s + dx * off, PropKind::Hydrant, rng, 200, true);
      }
    }
  }

  void park_props() {
    Pcg32 rng(sub_seed(cfg.seed, "park_props"));
    const cm spacing = cfg.park_tree_spacing_m * 100;
    // Dart throwing có giới hạn số lần thử -> deterministic, không vô hạn.
    for (int z = 0; z < w.dcells_z; ++z)
      for (int x = 0; x < w.dcells_x; ++x) {
        const District d = static_cast<District>(w.districts[static_cast<size_t>(z * w.dcells_x + x)]);
        const int tries = d == District::Park ? 10 : (d == District::Residential ? 1 : 0);
        for (int t = 0; t < tries; ++t) {
          const cm px = x * w.district_cell + rng.range(0, w.district_cell - 1);
          const cm pz = z * w.district_cell + rng.range(0, w.district_cell - 1);
          const uint32_t roll = rng.below(100);
          const PropKind k = roll < 55 ? PropKind::Tree : roll < 75 ? PropKind::Pine : roll < 92 ? PropKind::Bush : PropKind::Rock;
          add_prop(px, pz, k, rng, spacing, false);
        }
      }
    // Ghế quanh quảng trường
    for (const auto& pl : plazas) {
      for (int i = 0; i < 4; ++i) {
        const cm ox = (i & 1 ? 1 : -1) * 450, oz = (i & 2 ? 1 : -1) * 450;
        add_prop(pl.first + ox, pl.second + oz, PropKind::Bench, rng, 200, false);
      }
    }
    // Ven rìa thế giới (ngoài đường vành đai): đá và cây
    const cm m = cfg.margin_m * 100 - cfg.arterial_half_width_cm - cfg.sidewalk_cm;
    for (int i = 0; i < 120 && m > 300; ++i) {
      const bool horizontal = rng.chance(50);
      const bool high = rng.chance(50);
      const cm along = rng.range(0, horizontal ? w.size_x : w.size_z);
      const cm across = rng.range(100, m - 100);
      const cm x = horizontal ? along : (high ? w.size_x - across : across);
      const cm z = horizontal ? (high ? w.size_z - across : across) : along;
      add_prop(x, z, rng.chance(60) ? PropKind::Pine : PropKind::Rock, rng, 500, false);
    }
  }

  // ---------------------------------------------------------------- 8. POI
  void add_poi(cm x, cm z, PoiKind k) { pois.push_back({0, x, z, k, w.district_at(x, z)}); }

  bool poi_ok(cm x, cm z) const {
    if (w.in_pond(x, z, 50)) return false;
    for (const auto& r : building_rects)
      if (point_rect_dist2(x, z, r) < 100 * 100) return false;
    for (const auto& r : carriageways)
      if (r.contains(x, z)) return false;
    if (x <= 0 || z <= 0 || x >= w.size_x || z >= w.size_z) return false;
    for (const auto& p : pois)  // không trùng POI khác
      if (dist2(x, z, p.x, p.z) < 150LL * 150LL) return false;
    return reachable_nav(x, z) >= 0;
  }

  void points_of_interest() {
    Pcg32 rng(sub_seed(cfg.seed, "pois"));
    // Spawn: quanh quảng trường công viên (vùng safe)
    int spawned = 0;
    for (int ring = 0; spawned < cfg.spawn_count && ring < 8; ++ring)
      for (const auto& pl : plazas) {
        if (spawned >= cfg.spawn_count) break;
        // 8 hướng cố định (số nguyên, không dùng sin/cos -> giống nhau mọi nền tảng)
        static constexpr int kDir[8][2] = {{250, 0}, {177, 177}, {0, 250}, {-177, 177}, {-250, 0}, {-177, -177}, {0, -250}, {177, -177}};
        const auto& dir = kDir[(spawned * 3 + ring) % 8];
        const cm x = pl.first + dir[0] * (1 + ring / 2), z = pl.second + dir[1] * (1 + ring / 2);
        if (!poi_ok(x, z) || w.zone_at(x, z) != ZoneRule::Safe || !prop_grid.free(x, z, 100)) continue;
        add_poi(x, z, PoiKind::Spawn);
        ++spawned;
      }
    // Dự phòng: tâm các ô district thuộc vùng safe (duyệt theo thứ tự cố định).
    for (int z = 0; z < w.dcells_z && spawned < cfg.spawn_count; ++z)
      for (int x = 0; x < w.dcells_x && spawned < cfg.spawn_count; ++x) {
        const cm px = x * w.district_cell + w.district_cell / 2, pz = z * w.district_cell + w.district_cell / 2;
        if (w.zone_at(px, pz) != ZoneRule::Safe || !poi_ok(px, pz) || !prop_grid.free(px, pz, 100)) continue;
        add_poi(px, pz, PoiKind::Spawn);
        ++spawned;
      }
    // Ứng viên theo district
    std::array<std::vector<std::pair<cm, cm>>, kDistrictCount> water_c, food_c;
    std::vector<std::pair<cm, cm>> shelter_c;
    for (const auto& p : w.ponds)
      for (int i = 0; i < 4; ++i) {
        static constexpr int kDir[4][2] = {{92, 39}, {-39, 92}, {-92, -39}, {39, -92}};  // ~cos/sin(i*90°+23°) * 100
        const cm r = p.radius + 120;
        water_c[static_cast<size_t>(District::Park)].emplace_back(p.x + kDir[i][0] * r / 100, p.z + kDir[i][1] * r / 100);
      }
    for (const auto& pl : plazas) water_c[static_cast<size_t>(District::Park)].emplace_back(pl.first, pl.second);
    for (const auto& pr : props) {
      const District d = w.district_at(pr.x, pr.z);
      if (pr.kind == PropKind::Hydrant || pr.kind == PropKind::Lamp) water_c[static_cast<size_t>(d)].emplace_back(pr.x + 120, pr.z);
      if (pr.kind == PropKind::TrashBin || pr.kind == PropKind::Dumpster) food_c[static_cast<size_t>(d)].emplace_back(pr.x, pr.z + 120);
      if (pr.kind == PropKind::Bench) food_c[static_cast<size_t>(d)].emplace_back(pr.x + 100, pr.z);
    }
    for (const auto& b : buildings) {
      const auto fp = front_point(b, 120);
      if (b.district == District::Downtown) food_c[static_cast<size_t>(b.district)].push_back(fp);
      if (b.district == District::Industrial) shelter_c.push_back(fp);
      if (b.district == District::Residential && b.height < 600) food_c[static_cast<size_t>(b.district)].push_back(fp);
    }
    for (const auto& pl : plazas) shelter_c.emplace_back(pl.first + 800, pl.second + 800);

    auto pick = [&](std::vector<std::pair<cm, cm>>& c, int count, PoiKind k) {
      for (int n = 0; n < count && !c.empty();) {
        const size_t i = rng.below(static_cast<uint32_t>(c.size()));
        const auto p = c[i];
        c.erase(c.begin() + static_cast<std::ptrdiff_t>(i));
        if (!poi_ok(p.first, p.second)) continue;
        add_poi(p.first, p.second, k);
        ++n;
      }
    };
    for (int d = 0; d < kDistrictCount; ++d) {
      pick(water_c[static_cast<size_t>(d)], rng.range(cfg.water_min, cfg.water_max), PoiKind::Water);
      pick(food_c[static_cast<size_t>(d)], rng.range(cfg.food_min, cfg.food_max), PoiKind::Food);
    }
    pick(shelter_c, rng.range(cfg.shelter_min, cfg.shelter_max), PoiKind::Shelter);
    // Cửa hàng: một số nhà trung tâm
    std::vector<std::pair<cm, cm>> shops;
    for (const auto& b : buildings)
      if (b.district == District::Downtown && b.height < 2000) shops.push_back(front_point(b, 120));
    pick(shops, 3, PoiKind::Shop);
  }

  // ---------------------------------------------------------------- 9. navigation
  std::map<std::pair<cm, cm>, uint32_t> nav_id;
  size_t road_nav_count = 0;
  uint32_t nav_node(cm x, cm z) {
    auto [it, ins] = nav_id.try_emplace({x, z}, static_cast<uint32_t>(w.nav_nodes.size()));
    if (ins) w.nav_nodes.push_back({x, z});
    return it->second;
  }

  // Nav graph = tim đường chia nhỏ ≤ 20 m. Dựng TRƯỚC khi đặt POI để kiểm tra POI có tới được không.
  void road_navigation() {
    for (const auto& e : w.road_edges) {
      const auto& A = w.road_nodes[e.a];
      const auto& B = w.road_nodes[e.b];
      const auto len = static_cast<int64_t>(std::sqrt(static_cast<double>(dist2(A.x, A.z, B.x, B.z))));
      const int n = std::max<int>(1, static_cast<int>((len + 1999) / 2000));
      uint32_t prev = nav_node(A.x, A.z);
      for (int i = 1; i <= n; ++i) {
        const uint32_t cur = nav_node(static_cast<cm>(A.x + static_cast<int64_t>(B.x - A.x) * i / n),
                                      static_cast<cm>(A.z + static_cast<int64_t>(B.z - A.z) * i / n));
        w.nav_edges.push_back({prev, cur});
        prev = cur;
      }
    }
    road_nav_count = w.nav_nodes.size();
  }

  bool segment_clear(cm ax, cm az, cm bx, cm bz) const {
    for (const auto& r : building_rects)
      if (segment_hits_rect(ax, az, bx, bz, r)) return false;
    for (const auto& p : w.ponds)
      if (point_segment_dist2(p.x, p.z, ax, az, bx, bz) < static_cast<double>(p.radius) * p.radius) return false;
    return true;
  }

  // Nút nav đường gần nhất mà đoạn nối tới đó không xuyên nhà/hồ; -1 nếu không có trong 60 m.
  int64_t reachable_nav(cm x, cm z) const {
    int64_t best = -1, bd = 6000LL * 6000LL;
    for (size_t i = 0; i < road_nav_count; ++i) {
      const int64_t d = dist2(x, z, w.nav_nodes[i].x, w.nav_nodes[i].z);
      if (d < bd && segment_clear(x, z, w.nav_nodes[i].x, w.nav_nodes[i].z)) {
        bd = d;
        best = static_cast<int64_t>(i);
      }
    }
    return best;
  }

  void connect_pois() {
    for (const auto& p : pois) {
      const int64_t best = reachable_nav(p.x, p.z);
      if (best < 0) continue;  // không xảy ra: poi_ok đã kiểm tra
      const auto& q = w.nav_nodes[static_cast<size_t>(best)];
      const cm qx = q.x, qz = q.z;
      const auto len = static_cast<int64_t>(std::sqrt(static_cast<double>(dist2(p.x, p.z, qx, qz))));
      const int n = std::max<int>(1, static_cast<int>((len + 1999) / 2000));
      uint32_t prev = static_cast<uint32_t>(best);
      for (int i = 1; i <= n; ++i) {
        const uint32_t cur = nav_node(static_cast<cm>(qx + static_cast<int64_t>(p.x - qx) * i / n),
                                      static_cast<cm>(qz + static_cast<int64_t>(p.z - qz) * i / n));
        if (cur != prev) w.nav_edges.push_back({prev, cur});
        prev = cur;
      }
    }
  }

  // ---------------------------------------------------------------- 10-11. chunk + ID ổn định
  void chunkify() {
    const cm cs = w.chunk_size;
    w.chunks.resize(static_cast<size_t>(w.chunks_x * w.chunks_z));
    for (int cz = 0; cz < w.chunks_z; ++cz)
      for (int cx = 0; cx < w.chunks_x; ++cx) {
        auto& c = w.chunks[static_cast<size_t>(w.chunk_index(cx, cz))];
        c.cx = cx;
        c.cz = cz;
      }
    auto chunk_of = [&](cm x, cm z) -> ChunkData& {
      const int cx = std::clamp(static_cast<int>(x / cs), 0, w.chunks_x - 1);
      const int cz = std::clamp(static_cast<int>(z / cs), 0, w.chunks_z - 1);
      return w.chunks[static_cast<size_t>(w.chunk_index(cx, cz))];
    };
    for (const auto& b : buildings) chunk_of(b.x, b.z).buildings.push_back(b);
    for (const auto& p : props) chunk_of(p.x, p.z).props.push_back(p);
    for (const auto& p : pois) chunk_of(p.x, p.z).pois.push_back(p);
    for (size_t ci = 0; ci < w.chunks.size(); ++ci) {
      auto& c = w.chunks[ci];
      auto key = [](auto const& v) { return std::tuple(v.z, v.x); };
      std::sort(c.buildings.begin(), c.buildings.end(), [&](auto& a, auto& b) { return key(a) < key(b); });
      std::sort(c.props.begin(), c.props.end(), [&](auto& a, auto& b) {
        return std::tuple(a.z, a.x, static_cast<int>(a.kind)) < std::tuple(b.z, b.x, static_cast<int>(b.kind));
      });
      std::sort(c.pois.begin(), c.pois.end(), [&](auto& a, auto& b) {
        return std::tuple(a.z, a.x, static_cast<int>(a.kind)) < std::tuple(b.z, b.x, static_cast<int>(b.kind));
      });
      const auto chunk_bits = static_cast<uint32_t>(ci) << 16;
      for (size_t i = 0; i < c.buildings.size(); ++i) c.buildings[i].id = (1u << 28) | chunk_bits | static_cast<uint32_t>(i);
      for (size_t i = 0; i < c.props.size(); ++i) c.props[i].id = (2u << 28) | chunk_bits | static_cast<uint32_t>(i);
      for (size_t i = 0; i < c.pois.size(); ++i) c.pois[i].id = (3u << 28) | chunk_bits | static_cast<uint32_t>(i);
    }
  }

  WorldData run() {
    w.seed = cfg.seed;
    w.config_hash = world_config_hash(cfg);
    w.size_x = cfg.size_m_x * 100;
    w.size_z = cfg.size_m_z * 100;
    w.chunk_size = cfg.chunk_size_m * 100;
    w.chunks_x = (w.size_x + w.chunk_size - 1) / w.chunk_size;
    w.chunks_z = (w.size_z + w.chunk_size - 1) / w.chunk_size;
    w.zone_rules = cfg.zone_rules;
    districts();
    terrain();
    roads();
    blocks_and_buildings();
    street_props();
    park_props();
    road_navigation();
    points_of_interest();
    connect_pois();
    chunkify();
    w.stats.road_nodes = static_cast<uint32_t>(w.road_nodes.size());
    w.stats.road_edges = static_cast<uint32_t>(w.road_edges.size());
    w.stats.buildings = static_cast<uint32_t>(buildings.size());
    w.stats.props = static_cast<uint32_t>(props.size());
    w.stats.pois = static_cast<uint32_t>(pois.size());
    w.stats.nav_nodes = static_cast<uint32_t>(w.nav_nodes.size());
    w.stats.ponds = static_cast<uint32_t>(w.ponds.size());
    return std::move(w);
  }
};
}  // namespace

WorldData generate_world(const WorldConfig& cfg) {
  Gen g(cfg);
  return g.run();
}

}  // namespace pb::world
