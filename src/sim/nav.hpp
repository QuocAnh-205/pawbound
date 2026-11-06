#pragma once
// Waypoint graph từ world generator (ADR-002/Phase 5) + A* có giới hạn + cache đường đi.
// LƯU Ý: đây KHÔNG phải navmesh. NPC đi theo tim đường/lối đi; đoạn cuối tới POI là đường nối đã kiểm tra không xuyên nhà/hồ.
#include <cstdint>
#include <list>
#include <optional>
#include <unordered_map>
#include <vector>

#include <glm/vec2.hpp>

#include "world/world_types.hpp"

namespace pb::sim {

struct PathResult {
  enum class Status { Found, NoPath, Truncated, InvalidNode };
  Status status = Status::NoPath;
  std::vector<uint32_t> nodes;
  float length = 0;
  uint32_t expanded = 0;
};
const char* to_string(PathResult::Status s);

class NavGraph {
 public:
  explicit NavGraph(const world::WorldData& w);
  NavGraph(std::vector<glm::vec2> nodes, const std::vector<std::pair<uint32_t, uint32_t>>& edges);

  [[nodiscard]] size_t size() const { return pos_.size(); }
  [[nodiscard]] glm::vec2 pos(uint32_t n) const { return pos_[n]; }
  [[nodiscard]] const std::vector<uint32_t>& neighbors(uint32_t n) const { return adj_[n]; }
  // Nút gần nhất (lưới 16 m); nullopt nếu không có nút trong max_dist.
  [[nodiscard]] std::optional<uint32_t> nearest(glm::vec2 p, float max_dist = 60.0f) const;
  // A* với giới hạn số nút mở rộng (chống treo khi không có đường / đồ thị lớn).
  PathResult find_path(uint32_t start, uint32_t goal, uint32_t max_expanded = 4000) const;
  // Có cache LRU theo (start, goal): đường đã tìm không tính lại mỗi frame.
  const PathResult& find_path_cached(uint32_t start, uint32_t goal);
  [[nodiscard]] size_t cache_hits() const { return hits_; }
  [[nodiscard]] size_t cache_misses() const { return misses_; }

 private:
  void build_grid();
  std::vector<glm::vec2> pos_;
  std::vector<std::vector<uint32_t>> adj_;
  float cell_ = 16.0f;
  std::unordered_map<uint64_t, std::vector<uint32_t>> grid_;
  // LRU cache
  size_t cache_cap_ = 512;
  std::list<uint64_t> lru_;
  std::unordered_map<uint64_t, std::pair<PathResult, std::list<uint64_t>::iterator>> cache_;
  size_t hits_ = 0, misses_ = 0;
};

}  // namespace pb::sim
