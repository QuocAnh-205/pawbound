#include "net/netsim.hpp"

namespace pb::net {

double NetSim::rnd() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 7;
  rng_ ^= rng_ << 17;
  return static_cast<double>(rng_ >> 11) / 9007199254740992.0;
}

void NetSim::push(double now, std::vector<uint8_t> pkt, SendFn send) {
  if (!active()) {
    send(pkt);
    return;
  }
  if (rnd() < s_.loss) {
    ++dropped_;
    return;
  }
  const double delay = (s_.latency_ms + (rnd() * 2.0 - 1.0) * s_.jitter_ms) / 1000.0;
  q_.push({now + std::max(0.0, delay), order_++, std::move(pkt), std::move(send)});
}

void NetSim::pump(double now) {
  while (!q_.empty() && q_.top().due <= now) {
    const Item& it = q_.top();
    it.send(it.pkt);
    q_.pop();
  }
}

}  // namespace pb::net
