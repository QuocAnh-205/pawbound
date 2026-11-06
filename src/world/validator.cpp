#include "world/validator.hpp"

#include <format>
#include <map>
#include <queue>
#include <set>

#include "world/geometry2d.hpp"
#include "world/world_io.hpp"

namespace pb::world {
namespace {

std::vector<uint32_t> bfs(size_t n, const std::vector<std::pair<uint32_t, uint32_t>>& edges, uint32_t start,
                          std::vector<std::vector<uint32_t>>& adj) {
  adj.assign(n, {});
  for (const auto& [a, b] : edges) {
    adj[a].push_back(b);
    adj[b].push_back(a);
  }
  std::vector<uint32_t> comp(n, UINT32_MAX);
  if (n == 0) return comp;
  std::queue<uint32_t> q;
  q.push(start);
  comp[start] = 0;
  while (!q.empty()) {
    const uint32_t u = q.front();
    q.pop();
    for (uint32_t v : adj[u])
      if (comp[v] == UINT32_MAX) {
        comp[v] = 0;
        q.push(v);
      }
  }
  return comp;
}

}  // namespace

std::vector<WorldIssue> validate_world(const WorldData& w, const ValidateOptions& opt) {
  std::vector<WorldIssue> out;
  std::map<std::string, int> counts;
  auto issue = [&](const char* cat, std::string msg) {
    if (++counts[cat] <= opt.max_issues_per_category) out.push_back({cat, std::move(msg)});
  };

  // ---- road connectivity
  {
    std::vector<std::pair<uint32_t, uint32_t>> e;
    for (const auto& r : w.road_edges) e.emplace_back(r.a, r.b);
    std::vector<std::vector<uint32_t>> adj;
    const auto reach = bfs(w.road_nodes.size(), e, 0, adj);
    if (w.road_nodes.empty()) issue("road_connectivity", "không có nút đường");
    for (size_t i = 0; i < reach.size(); ++i)
      if (reach[i] == UINT32_MAX)
        issue("road_connectivity", std::format("nút đường {} tại ({:.1f}, {:.1f}) m không nối được tới mạng chính", i,
                                               to_m(w.road_nodes[i].x), to_m(w.road_nodes[i].z)));
  }

  // Thu thập instance + kiểm tra chunk
  std::vector<const Building*> bs;
  std::vector<const Prop*> ps;
  std::vector<const Poi*> pois;
  std::set<uint32_t> ids;
  if (w.chunks.size() != static_cast<size_t>(w.chunks_x * w.chunks_z)) issue("chunk", "số chunk không khớp lưới");
  for (const auto& c : w.chunks) {
    const Rect cr{c.cx * w.chunk_size, c.cz * w.chunk_size, (c.cx + 1) * w.chunk_size, (c.cz + 1) * w.chunk_size};
    auto in_chunk = [&](cm x, cm z) {
      const bool last_x = c.cx == w.chunks_x - 1, last_z = c.cz == w.chunks_z - 1;
      return x >= cr.x0 && (x < cr.x1 || last_x) && z >= cr.z0 && (z < cr.z1 || last_z);
    };
    for (const auto& b : c.buildings) {
      bs.push_back(&b);
      if (!in_chunk(b.x, b.z)) issue("chunk", std::format("nhà {:#x} nằm ngoài chunk ({},{})", b.id, c.cx, c.cz));
      if (!ids.insert(b.id).second) issue("chunk", std::format("trùng id {:#x}", b.id));
      if (b.asset >= w.asset_ids.size()) issue("chunk", std::format("nhà {:#x} trỏ asset {} không tồn tại", b.id, b.asset));
    }
    for (const auto& p : c.props) {
      ps.push_back(&p);
      if (!in_chunk(p.x, p.z)) issue("chunk", std::format("prop {:#x} nằm ngoài chunk ({},{})", p.id, c.cx, c.cz));
      if (!ids.insert(p.id).second) issue("chunk", std::format("trùng id {:#x}", p.id));
    }
    for (const auto& p : c.pois) {
      pois.push_back(&p);
      if (!in_chunk(p.x, p.z)) issue("chunk", std::format("POI {:#x} nằm ngoài chunk ({},{})", p.id, c.cx, c.cz));
      if (!ids.insert(p.id).second) issue("chunk", std::format("trùng id {:#x}", p.id));
    }
  }

  // ---- building overlap (sweep theo x)
  {
    std::vector<std::pair<Rect, uint32_t>> rs;
    for (const auto* b : bs) rs.emplace_back(building_rect(*b), b->id);
    std::sort(rs.begin(), rs.end(), [](auto& a, auto& b) { return a.first.x0 < b.first.x0; });
    for (size_t i = 0; i < rs.size(); ++i)
      for (size_t j = i + 1; j < rs.size() && rs[j].first.x0 < rs[i].first.x1; ++j)
        if (rs[i].first.overlaps(rs[j].first))
          issue("building_overlap", std::format("nhà {:#x} chồng lấn nhà {:#x}", rs[i].second, rs[j].second));
  }

  // ---- building ∩ road / pond; prop / POI trong vật cản
  std::vector<Rect> full, carriage;
  std::vector<std::pair<RoadNode, RoadNode>> paths;
  for (const auto& e : w.road_edges) {
    const auto& a = w.road_nodes[e.a];
    const auto& b = w.road_nodes[e.b];
    if (e.cls == RoadClass::Path) {
      paths.emplace_back(a, b);
      continue;
    }
    full.push_back(road_rect(a, b, e.half_width + e.sidewalk));
    carriage.push_back(road_rect(a, b, e.half_width));
  }
  for (const auto* b : bs) {
    const Rect r = building_rect(*b);
    for (const auto& f : full)
      if (f.overlaps(r)) {
        issue("building_road", std::format("nhà {:#x} tại ({:.1f}, {:.1f}) m lấn đường/vỉa hè", b->id, to_m(b->x), to_m(b->z)));
        break;
      }
    for (const auto& p : paths)
      if (segment_hits_rect(p.first.x, p.first.z, p.second.x, p.second.z, r)) {
        issue("building_road", std::format("nhà {:#x} chắn lối đi", b->id));
        break;
      }
    if (w.in_pond(r.cx(), r.cz())) issue("building_road", std::format("nhà {:#x} nằm trong hồ", b->id));
  }
  for (const auto* p : ps) {
    const bool street = p->kind == PropKind::Lamp || p->kind == PropKind::TrashBin || p->kind == PropKind::Hydrant ||
                        p->kind == PropKind::Tree;
    for (const auto* b : bs)
      if (building_rect(*b).contains(p->x, p->z)) {
        issue("prop_obstacle", std::format("{} {:#x} nằm trong nhà {:#x}", to_string(p->kind), p->id, b->id));
        break;
      }
    for (const auto& r : street ? carriage : full)
      if (r.contains(p->x, p->z)) {
        issue("prop_obstacle", std::format("{} {:#x} tại ({:.1f}, {:.1f}) m nằm trên lòng đường", to_string(p->kind), p->id,
                                           to_m(p->x), to_m(p->z)));
        break;
      }
    if (w.in_pond(p->x, p->z)) issue("prop_obstacle", std::format("{} {:#x} nằm trong hồ", to_string(p->kind), p->id));
  }

  // ---- POI / spawn reachable qua nav graph
  {
    std::vector<std::pair<uint32_t, uint32_t>> e;
    for (const auto& n : w.nav_edges) e.emplace_back(n.a, n.b);
    std::vector<std::vector<uint32_t>> adj;
    const auto reach = bfs(w.nav_nodes.size(), e, 0, adj);
    std::map<std::pair<cm, cm>, uint32_t> nav_at;
    for (size_t i = 0; i < w.nav_nodes.size(); ++i) nav_at[{w.nav_nodes[i].x, w.nav_nodes[i].z}] = static_cast<uint32_t>(i);
    int spawns = 0;
    for (const auto* p : pois) {
      auto it = nav_at.find({p->x, p->z});
      if (it == nav_at.end() || reach[it->second] == UINT32_MAX) {
        issue("poi_unreachable", std::format("{} {:#x} tại ({:.1f}, {:.1f}) m không có đường tới", to_string(p->kind), p->id,
                                             to_m(p->x), to_m(p->z)));
        continue;
      }
      // Cạnh nối POI vào nav không được xuyên nhà/hồ, dài tối đa 30 m
      for (uint32_t nb : adj[it->second]) {
        for (const auto& pd : w.ponds)
          if (point_segment_dist2(pd.x, pd.z, p->x, p->z, w.nav_nodes[nb].x, w.nav_nodes[nb].z) <
              static_cast<double>(pd.radius) * pd.radius)
            issue("poi_unreachable", std::format("{} {:#x}: lối vào cắt qua hồ", to_string(p->kind), p->id));
        const auto& q = w.nav_nodes[nb];
        if (dist2(p->x, p->z, q.x, q.z) > 3000LL * 3000LL)
          issue("poi_unreachable", std::format("{} {:#x}: nút nav gần nhất xa hơn 30 m", to_string(p->kind), p->id));
        for (const auto* b : bs)
          if (segment_hits_rect(p->x, p->z, q.x, q.z, building_rect(*b))) {
            issue("poi_unreachable", std::format("{} {:#x}: lối vào bị nhà {:#x} chắn", to_string(p->kind), p->id, b->id));
            break;
          }
      }
      for (const auto* b : bs)
        if (building_rect(*b).contains(p->x, p->z)) issue("poi_unreachable", std::format("POI {:#x} nằm trong nhà", p->id));
      if (p->kind == PoiKind::Spawn) {
        ++spawns;
        if (w.zone_at(p->x, p->z) != ZoneRule::Safe)
          issue("spawn", std::format("spawn {:#x} không nằm trong vùng safe", p->id));
        if (w.in_pond(p->x, p->z, 50)) issue("spawn", std::format("spawn {:#x} nằm trên nước", p->id));
        for (const auto* pr : ps)
          if (dist2(p->x, p->z, pr->x, pr->z) < 100LL * 100LL)
            issue("spawn", std::format("spawn {:#x} quá gần vật cản {} {:#x}", p->id, to_string(pr->kind), pr->id));
        for (const auto& r : carriage)
          if (r.contains(p->x, p->z)) issue("spawn", std::format("spawn {:#x} nằm trên lòng đường", p->id));
      }
    }
    if (spawns == 0) issue("spawn", "không có spawn point");
  }

  // ---- hash
  if (!opt.expected_hash.empty()) {
    const std::string h = logical_hash(w);
    if (h != opt.expected_hash) issue("hash", std::format("logical hash {} khác giá trị đã lưu {}", h, opt.expected_hash));
  }
  for (const auto& [cat, n] : counts)
    if (n > opt.max_issues_per_category)
      out.push_back({cat, std::format("... và {} lỗi khác cùng loại", n - opt.max_issues_per_category)});
  return out;
}

}  // namespace pb::world
