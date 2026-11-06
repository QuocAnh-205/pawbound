#pragma once
// PRNG và hash deterministic (ADR-007). Chỉ dùng phép toán số nguyên — cùng kết quả trên mọi compiler/OS.
#include <cstdint>
#include <string_view>

namespace pb::world {

constexpr uint64_t splitmix64(uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

constexpr uint64_t fnv1a64(std::string_view s) {
  uint64_t h = 0xcbf29ce484222325ull;
  for (char c : s) {
    h ^= static_cast<uint8_t>(c);
    h *= 0x100000001b3ull;
  }
  return h;
}

// Sub-seed cho từng bước sinh: thêm/sửa một bước không làm xáo trộn bước khác.
constexpr uint64_t sub_seed(uint64_t seed, std::string_view stage, uint64_t a = 0, uint64_t b = 0) {
  return splitmix64(splitmix64(seed ^ fnv1a64(stage)) ^ splitmix64(a * 0x9E3779B97F4A7C15ull + b));
}

// PCG32 (O'Neill, XSH-RR). Thuật toán công khai.
class Pcg32 {
 public:
  explicit Pcg32(uint64_t seed, uint64_t stream = 0x14057B7EF767814Full) {
    inc_ = (stream << 1u) | 1u;
    next_u32();
    state_ += seed;
    next_u32();
  }
  uint32_t next_u32() {
    const uint64_t old = state_;
    state_ = old * 6364136223846793005ull + inc_;
    const auto xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
    const auto rot = static_cast<uint32_t>(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
  }
  // Số nguyên đều trong [0, bound) — không bias (Lemire/rejection).
  uint32_t below(uint32_t bound) {
    if (bound <= 1) return 0;
    const uint32_t threshold = (0u - bound) % bound;
    for (;;) {
      const uint32_t r = next_u32();
      if (r >= threshold) return r % bound;
    }
  }
  // Số nguyên đều trong [lo, hi] (bao gồm hai đầu).
  int32_t range(int32_t lo, int32_t hi) {
    if (hi <= lo) return lo;
    return lo + static_cast<int32_t>(below(static_cast<uint32_t>(hi - lo) + 1u));
  }
  // true với xác suất percent/100.
  bool chance(uint32_t percent) { return below(100) < percent; }

 private:
  uint64_t state_ = 0;
  uint64_t inc_ = 0;
};

}  // namespace pb::world
