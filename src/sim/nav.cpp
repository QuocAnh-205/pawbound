#include "sim/nav.hpp"

#include <algorithm>
#include <cmath>
#include <queue>

#include <glm/geometric.hpp>

namespace pb::sim {

const char* to_string(PathResult::Status s) {
  switch (s) {
    case PathResult::Status::Found: return "found";
    case PathResult::Status::NoPath: return "no_path";
    case PathResult::Status::Truncated: return "truncated";
    case PathResult::Status::InvalidNode: return "invalid_node";
  }
  return "?";
}

namespace {
uint64_t gkey(int64_t x, int64_t z) { return (static_cast<uint64_t>(x + (1 << 20)) << 32) | static_cast<uint64_t>(z + (1 << 20)); }
}  // namespace

NavGraph::NavGraph(const world::WorldData& w) {
  pos_.reserve(w.nav_nodes.size());
  for (const auto& n : w.nav_nodes) pos_.emplace_back(world::to_m(n.x), world::to_m(n.z));
  adj_.resize(pos_.size());
  for (const auto& e : w.nav_edges) {
    if (e.a == e.b) continue;
    adj_[e.a].push_back(e.b);
    adj_[e.b].push_back(e.a);
  }
  build_grid();
}

NavGraph::NavGraph(std::vector<glm::vec2> nodes, const std::vector<std::pair<uint32_t, uint32_t>>& edges)
    : pos_(std::move(nodes)), adj_(pos_.size()) {
  for (const auto& [a, b] : edges) {
    adj_[a].push_back(b);
    adj_[b].push_back(a);
  }
  build_grid();
}

void NavGraph::build_grid() {
  for (size_t i = 0; i < pos_.size(); ++i)
    grid_[gkey(static_cast<int64_t>(std::floor(pos_[i].x / cell_)), static_cast<int64_t>(std::floor(pos_[i].y / cell_)))]
        .push_back(static_cast<uint32_t>(i));
}

std::optional<uint32_t> NavGraph::nearest(glm::vec2 p, float max_dist) const {
  const auto cx = static_cast<int64_t>(std::floor(p.x / cell_)), cz = static_cast<int64_t>(std::floor(p.y / cell_));
  const int64_t r = static_cast<int64_t>(std::ceil(max_dist / cell_));
  std::optional<uint32_t> best;
  float bd = max_dist;
  for (int64_t z = cz - r; z <= cz + r; ++z)
    for (int64_t x = cx - r; x <= cx + r; ++x) {
      auto it = grid_.find(gkey(x, z));
      if (it == grid_.end()) continue;
      for (uint32_t n : it->second) {
        const float d = glm::distance(p, pos_[n]);
        if (d < bd) {
          bd = d;
          best = n;
        }
      }
    }
  return best;
}

PathResult NavGraph::find_path(uint32_t start, uint32_t goal, uint32_t max_expanded) const {
  PathResult r;
  if (start >= pos_.size() || goal >= pos_.size()) {
    r.status = PathResult::Status::InvalidNode;
    return r;
  }
  if (start == goal) {
    r.status = PathResult::Status::Found;
    r.nodes = {start};
    return r;
  }
  const size_t n = pos_.size();
  std::vector<float> g(n, INFINITY);
  std::vector<uint32_t> parent(n, UINT32_MAX);
  std::vector<uint8_t> closed(n, 0);
  using Item = std::pair<float, uint32_t>;
  std::priority_queue<Item, std::vector<Item>, std::greater<>> open;
  g[start] = 0;
  open.emplace(glm::distance(pos_[start], pos_[goal]), start);
  while (!open.empty()) {
    const auto [f, u] = open.top();
    open.pop();
    if (closed[u]) continue;
    closed[u] = 1;
    if (++r.expanded > max_expanded) {
      r.status = PathResult::Status::Truncated;
      return r;
    }
    if (u == goal) break;
    for (uint32_t v : adj_[u]) {
      if (closed[v]) continue;
      const float ng = g[u] + glm::distance(pos_[u], pos_[v]);
      if (ng < g[v]) {
        g[v] = ng;
        parent[v] = u;
        open.emplace(ng + glm::distance(pos_[v], pos_[goal]), v);
      }
    }
  }
  if (!closed[goal]) {
    r.status = PathResult::Status::NoPath;
    return r;
  }
  for (uint32_t v = goal; v != UINT32_MAX; v = parent[v]) {
    r.nodes.push_back(v);
    if (r.nodes.size() > n) {  // phòng thủ: không thể xảy ra với đồ thị hợp lệ
      r.status = PathResult::Status::NoPath;
      r.nodes.clear();
      return r;
    }
  }
  std::reverse(r.nodes.begin(), r.nodes.end());
  r.length = g[goal];
  r.status = PathResult::Status::Found;
  return r;
}

const PathResult& NavGraph::find_path_cached(uint32_t start, uint32_t goal) {
  const uint64_t key = (static_cast<uint64_t>(start) << 32) | goal;
  if (auto it = cache_.find(key); it != cache_.end()) {
    ++hits_;
    lru_.splice(lru_.begin(), lru_, it->second.second);
    return it->second.first;
  }
  ++misses_;
  if (cache_.size() >= cache_cap_) {
    cache_.erase(lru_.back());
    lru_.pop_back();
  }
  lru_.push_front(key);
  auto [it, _] = cache_.emplace(key, std::make_pair(find_path(start, goal), lru_.begin()));
  return it->second.first;
}

}  // namespace pb::sim
