#pragma once
// Mô phỏng mạng xấu ở tầng ứng dụng (để test): mất gói, trễ, jitter cho gói UNRELIABLE gửi đi.
// Không phụ thuộc công cụ hệ điều hành. Deterministic theo seed.
#include <cstdint>
#include <functional>
#include <queue>
#include <algorithm>
#include <vector>

namespace pb::net {

struct NetSimSettings {
  double loss = 0.0;        // 0..1
  double latency_ms = 0.0;  // một chiều
  double jitter_ms = 0.0;
};

class NetSim {
 public:
  using SendFn = std::function<void(const std::vector<uint8_t>&)>;
  explicit NetSim(NetSimSettings s = {}, uint64_t seed = 1) : s_(s), rng_(seed) {}
  [[nodiscard]] bool active() const { return s_.loss > 0 || s_.latency_ms > 0 || s_.jitter_ms > 0; }
  // Đưa gói vào hàng đợi (có thể bị bỏ). now: giây.
  void push(double now, std::vector<uint8_t> pkt, SendFn send);
  // Gửi các gói đã tới hạn.
  void pump(double now);
  [[nodiscard]] uint64_t dropped() const { return dropped_; }

 private:
  double rnd();
  struct Item {
    double due;
    uint64_t order;
    std::vector<uint8_t> pkt;
    SendFn send;
    bool operator>(const Item& o) const { return due != o.due ? due > o.due : order > o.order; }
  };
  NetSimSettings s_;
  uint64_t rng_;
  uint64_t order_ = 0, dropped_ = 0;
  std::priority_queue<Item, std::vector<Item>, std::greater<>> q_;
};

}  // namespace pb::net
