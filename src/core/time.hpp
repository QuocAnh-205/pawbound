#pragma once
// Fixed timestep cho simulation, tách khỏi render delta.
#include <cmath>
#include <cstdint>
#include <optional>

namespace pb {

class FixedTimestep {
 public:
  // step: giây mỗi tick (vd 1/30). max_steps: số tick tối đa mỗi frame (chống "spiral of death").
  // max_frame_delta: delta lớn hơn (alt-tab, breakpoint) bị cắt.
  explicit FixedTimestep(double step = 1.0 / 30.0, int max_steps = 5, double max_frame_delta = 0.25)
      : step_(step), max_steps_(max_steps), max_frame_delta_(max_frame_delta) {}

  // Cộng thời gian frame, trả về số tick simulation cần chạy trong frame này.
  int advance(double frame_delta) {
    if (frame_delta < 0) frame_delta = 0;
    if (frame_delta > max_frame_delta_) {
      dropped_ += frame_delta - max_frame_delta_;
      frame_delta = max_frame_delta_;
    }
    accumulator_ += frame_delta;
    int steps = 0;
    while (accumulator_ >= step_ && steps < max_steps_) {
      accumulator_ -= step_;
      ++steps;
    }
    if (accumulator_ >= step_) {  // vẫn còn nợ sau max_steps -> bỏ, giữ phần lẻ để alpha đúng
      const double keep = std::fmod(accumulator_, step_);
      dropped_ += accumulator_ - keep;
      accumulator_ = keep;
    }
    total_steps_ += static_cast<uint64_t>(steps);
    return steps;
  }
  // Hệ số nội suy cho render, trong [0, 1).
  [[nodiscard]] double alpha() const { return accumulator_ / step_; }
  [[nodiscard]] double step() const { return step_; }
  [[nodiscard]] uint64_t total_steps() const { return total_steps_; }
  [[nodiscard]] double dropped_seconds() const { return dropped_; }

 private:
  double step_;
  int max_steps_;
  double max_frame_delta_;
  double accumulator_ = 0;
  double dropped_ = 0;
  uint64_t total_steps_ = 0;
};

// Thống kê frame theo cửa sổ thời gian (vd 5 giây).
class FrameStats {
 public:
  struct Report {
    double fps;
    double avg_ms;
    double max_ms;
    uint64_t frames;
    uint64_t ticks;
  };
  explicit FrameStats(double interval_seconds = 5.0) : interval_(interval_seconds) {}

  // Trả về report khi đủ một cửa sổ thời gian.
  std::optional<Report> add_frame(double frame_seconds, int ticks_this_frame) {
    elapsed_ += frame_seconds;
    sum_ += frame_seconds;
    if (frame_seconds > max_) max_ = frame_seconds;
    ++frames_;
    ticks_ += static_cast<uint64_t>(ticks_this_frame);
    if (elapsed_ < interval_) return std::nullopt;
    Report r{static_cast<double>(frames_) / elapsed_, sum_ / static_cast<double>(frames_) * 1000.0, max_ * 1000.0,
             frames_, ticks_};
    elapsed_ = sum_ = max_ = 0;
    frames_ = ticks_ = 0;
    return r;
  }

 private:
  double interval_;
  double elapsed_ = 0, sum_ = 0, max_ = 0;
  uint64_t frames_ = 0, ticks_ = 0;
};

}  // namespace pb
