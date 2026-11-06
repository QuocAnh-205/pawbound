#include "sim/collision.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/geometric.hpp>

namespace pb::sim {
namespace {
constexpr uint32_t kCircleBit = 0x80000000u;
uint64_t key(int64_t x, int64_t z) { return (static_cast<uint64_t>(x + (1 << 20)) << 32) | static_cast<uint64_t>(z + (1 << 20)); }

float prop_radius(world::PropKind k) {
  switch (k) {
    case world::PropKind::Tree:
    case world::PropKind::Pine: return 0.3f;
    case world::PropKind::Lamp: return 0.12f;
    case world::PropKind::Hydrant: return 0.2f;
    case world::PropKind::TrashBin: return 0.3f;
    case world::PropKind::Rock: return 0.6f;
    case world::PropKind::Bush: return 0.7f;
    default: return 0.0f;  // dùng box
  }
}
float prop_height(world::PropKind k) {
  switch (k) {
    case world::PropKind::Tree:
    case world::PropKind::Pine:
    case world::PropKind::Lamp: return 4.0f;
    case world::PropKind::Hydrant: return 0.7f;
    case world::PropKind::TrashBin: return 0.9f;
    case world::PropKind::Rock: return 0.35f;
    case world::PropKind::Bush: return 0.55f;
    case world::PropKind::Bench: return 0.5f;
    case world::PropKind::Dumpster: return 1.2f;
    case world::PropKind::Crate: return 0.8f;
    default: return 1.0f;
  }
}
}  // namespace

CollisionWorld::CollisionWorld(const world::WorldData& w) : world_(&w) {
  using world::to_m;
  size_ = {to_m(w.size_x), to_m(w.size_z)};
  for (const auto& c : w.chunks) {
    for (const auto& b : c.buildings) {
      const glm::vec2 mn(to_m(b.x - b.half_x), to_m(b.z - b.half_z)), mx(to_m(b.x + b.half_x), to_m(b.z + b.half_z));
      add_box({mn, mx, w.height_at(to_m(b.x), to_m(b.z)) + to_m(b.height) + 1.0f});
    }
    for (const auto& p : c.props) {
      const float x = to_m(p.x), z = to_m(p.z), g = w.height_at(x, z);
      const float s = static_cast<float>(p.scale_pct) / 100.0f;
      const float r = prop_radius(p.kind) * s;
      if (r > 0) {
        add_circle({{x, z}, r, g + prop_height(p.kind) * s});
      } else {
        // Ghế/thùng lớn: box gần đúng theo trục (bỏ qua góc xoay — sai số nhỏ chấp nhận được cho 2.5D).
        const glm::vec2 half = p.kind == world::PropKind::Bench ? glm::vec2(0.8f, 0.8f) : glm::vec2(1.0f, 1.0f);
        add_box({glm::vec2(x, z) - half * s, glm::vec2(x, z) + half * s, g + prop_height(p.kind) * s});
      }
    }
  }
  for (const auto& p : w.ponds)
    add_circle({{to_m(p.x), to_m(p.z)}, to_m(p.radius), std::numeric_limits<float>::max()});
}

void CollisionWorld::index_shape(glm::vec2 mn, glm::vec2 mx, uint32_t id) {
  const auto x0 = static_cast<int64_t>(std::floor(mn.x / cell_)), x1 = static_cast<int64_t>(std::floor(mx.x / cell_));
  const auto z0 = static_cast<int64_t>(std::floor(mn.y / cell_)), z1 = static_cast<int64_t>(std::floor(mx.y / cell_));
  for (int64_t z = z0; z <= z1; ++z)
    for (int64_t x = x0; x <= x1; ++x) grid_[key(x, z)].push_back(id);
}

void CollisionWorld::add_box(const StaticBox& b) {
  index_shape(b.min, b.max, static_cast<uint32_t>(boxes_.size()));
  boxes_.push_back(b);
}
void CollisionWorld::add_circle(const StaticCircle& c) {
  index_shape(c.center - glm::vec2(c.radius), c.center + glm::vec2(c.radius),
              static_cast<uint32_t>(circles_.size()) | kCircleBit);
  circles_.push_back(c);
}

template <class F>
void CollisionWorld::query(glm::vec2 mn, glm::vec2 mx, F&& f) const {
  // stamp chống xử lý một hình hai lần khi nó nằm ở nhiều ô
  ++query_id_;
  if (stamp_.size() < boxes_.size() + circles_.size()) stamp_.assign(boxes_.size() + circles_.size(), 0);
  const auto x0 = static_cast<int64_t>(std::floor(mn.x / cell_)), x1 = static_cast<int64_t>(std::floor(mx.x / cell_));
  const auto z0 = static_cast<int64_t>(std::floor(mn.y / cell_)), z1 = static_cast<int64_t>(std::floor(mx.y / cell_));
  for (int64_t z = z0; z <= z1; ++z)
    for (int64_t x = x0; x <= x1; ++x) {
      auto it = grid_.find(key(x, z));
      if (it == grid_.end()) continue;
      for (uint32_t id : it->second) {
        const size_t flat = (id & kCircleBit) ? boxes_.size() + (id & ~kCircleBit) : id;
        if (stamp_[flat] == query_id_) continue;
        stamp_[flat] = query_id_;
        f(id);
      }
    }
}

CollisionHit CollisionWorld::resolve(glm::vec2& pos, float r, float feet, float step) const {
  CollisionHit hit;
  for (int iter = 0; iter < 4; ++iter) {
    bool moved = false;
    query(pos - glm::vec2(r), pos + glm::vec2(r), [&](uint32_t id) {
      if (id & kCircleBit) {
        const auto& c = circles_[id & ~kCircleBit];
        if (c.top <= feet + step) return;  // bước/nhảy qua được
        const glm::vec2 d = pos - c.center;
        const float dist = glm::length(d);
        const float min = c.radius + r;
        if (dist >= min) return;
        const glm::vec2 n = dist > 1e-5f ? d / dist : glm::vec2(1, 0);
        pos = c.center + n * min;
        hit.hit = true;
        hit.normal += n;
        moved = true;
      } else {
        const auto& b = boxes_[id];
        if (b.top <= feet + step) return;
        const glm::vec2 closest = glm::clamp(pos, b.min, b.max);
        const glm::vec2 d = pos - closest;
        const float dist2 = glm::dot(d, d);
        if (dist2 >= r * r) return;
        glm::vec2 n;
        if (dist2 > 1e-10f) {
          const float dist = std::sqrt(dist2);
          n = d / dist;
          pos = closest + n * r;
        } else {
          // Tâm nằm trong box: đẩy ra cạnh gần nhất
          const float dl = pos.x - b.min.x, dr = b.max.x - pos.x, dd = pos.y - b.min.y, du = b.max.y - pos.y;
          const float m = std::min({dl, dr, dd, du});
          if (m == dl) n = {-1, 0};
          else if (m == dr) n = {1, 0};
          else if (m == dd) n = {0, -1};
          else n = {0, 1};
          pos = n.x != 0 ? glm::vec2(n.x < 0 ? b.min.x - r : b.max.x + r, pos.y)
                         : glm::vec2(pos.x, n.y < 0 ? b.min.y - r : b.max.y + r);
        }
        hit.hit = true;
        hit.normal += n;
        moved = true;
      }
    });
    if (!moved) break;
  }
  // Biên thế giới
  const glm::vec2 lo(r), hi = size_ - glm::vec2(r);
  if (pos.x < lo.x || pos.y < lo.y || pos.x > hi.x || pos.y > hi.y) {
    const glm::vec2 c = glm::clamp(pos, lo, hi);
    hit.normal += glm::normalize(c - pos + glm::vec2(1e-6f));
    pos = c;
    hit.hit = true;
  }
  if (glm::dot(hit.normal, hit.normal) > 1e-8f) hit.normal = glm::normalize(hit.normal);
  return hit;
}

bool CollisionWorld::overlaps(glm::vec2 pos, float r, float feet, float step) const {
  glm::vec2 p = pos;
  return resolve(p, r, feet, step).hit;
}

bool CollisionWorld::segment_blocked(glm::vec2 a, glm::vec2 b, float eye) const {
  const float len = glm::distance(a, b);
  const int n = std::max(1, static_cast<int>(std::ceil(len / 0.5f)));
  bool blocked = false;
  query(glm::min(a, b), glm::max(a, b), [&](uint32_t id) {
    if (blocked || (id & kCircleBit)) return;  // cây/đèn mảnh không che tầm nhìn
    const auto& bx = boxes_[id];
    if (bx.top <= eye) return;
    for (int i = 1; i < n; ++i) {  // bỏ hai đầu mút (người nhìn/người bị nhìn có thể đứng sát tường)
      const glm::vec2 p = a + (b - a) * (static_cast<float>(i) / static_cast<float>(n));
      if (p.x > bx.min.x && p.x < bx.max.x && p.y > bx.min.y && p.y < bx.max.y) {
        blocked = true;
        return;
      }
    }
  });
  return blocked;
}

float CollisionWorld::ground_height(float x, float z) const { return world_ ? world_->height_at(x, z) : 0.0f; }

}  // namespace pb::sim
