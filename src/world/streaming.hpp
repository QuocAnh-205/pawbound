#pragma once
// Chọn chunk cần load/unload quanh điểm quan tâm, có hysteresis (unload_radius > load_radius)
// để không load/unload liên tục khi đứng ở mép chunk. Không tạo trùng: mỗi chunk chỉ load một lần.
#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

namespace pb::world {

class ChunkStreamer {
 public:
  struct Update {
    std::vector<int> load;    // chỉ số chunk cần nạp (chưa có trong loaded)
    std::vector<int> unload;  // chỉ số chunk cần gỡ
  };

  ChunkStreamer(int chunks_x, int chunks_z, float chunk_size_m, int load_radius = 2, int unload_radius = 3)
      : cx_(chunks_x), cz_(chunks_z), size_(chunk_size_m), load_r_(load_radius),
        unload_r_(std::max(unload_radius, load_radius)) {}

  Update update(float x_m, float z_m) {
    Update u;
    const int fx = std::clamp(static_cast<int>(std::floor(x_m / size_)), 0, cx_ - 1);
    const int fz = std::clamp(static_cast<int>(std::floor(z_m / size_)), 0, cz_ - 1);
    for (auto it = loaded_.begin(); it != loaded_.end();) {
      const int x = *it % cx_, z = *it / cx_;
      if (std::max(std::abs(x - fx), std::abs(z - fz)) > unload_r_) {
        u.unload.push_back(*it);
        it = loaded_.erase(it);
      } else {
        ++it;
      }
    }
    for (int z = fz - load_r_; z <= fz + load_r_; ++z)
      for (int x = fx - load_r_; x <= fx + load_r_; ++x) {
        if (x < 0 || z < 0 || x >= cx_ || z >= cz_) continue;
        const int idx = z * cx_ + x;
        if (loaded_.insert(idx).second) u.load.push_back(idx);
      }
    return u;
  }
  [[nodiscard]] const std::set<int>& loaded() const { return loaded_; }

 private:
  int cx_, cz_;
  float size_;
  int load_r_, unload_r_;
  std::set<int> loaded_;
};

}  // namespace pb::world
