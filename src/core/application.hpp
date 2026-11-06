#pragma once
// Vòng đời ứng dụng: init → [frame: begin → fixed_update×N → update → render] → shutdown.
// Simulation chạy fixed timestep; render nhận alpha để nội suy.
#include <cstdint>

#include "core/time.hpp"

namespace pb {

struct AppSettings {
  double fixed_hz = 30.0;
  int max_steps_per_frame = 5;
  double max_frame_delta = 0.25;
  double stats_interval = 5.0;  // giây; 0 = không log thống kê
  const char* log_category = "app";
};

class Application {
 public:
  explicit Application(AppSettings settings = {});
  virtual ~Application() = default;
  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  // Chạy tới khi request_quit(). Trả exit code. Exception trong vòng lặp được bắt, log, shutdown sạch.
  int run();
  void request_quit(int exit_code = 0);
  [[nodiscard]] bool quit_requested() const { return quit_; }
  [[nodiscard]] uint64_t frame_count() const { return frames_; }
  [[nodiscard]] uint64_t tick_count() const { return timestep_.total_steps(); }
  [[nodiscard]] const FixedTimestep& timestep() const { return timestep_; }

 protected:
  virtual bool on_init() = 0;  // false -> run() trả 1 (vẫn gọi on_shutdown)
  virtual void on_frame_begin() {}
  virtual void on_fixed_update(double dt) { (void)dt; }
  virtual void on_update(double dt) { (void)dt; }
  virtual void on_render(double alpha) { (void)alpha; }
  virtual void on_shutdown() {}
  virtual void on_stats(const FrameStats::Report& r);
  // Nguồn thời gian (giây, đơn điệu). Test override để điều khiển thời gian.
  virtual double now_seconds();

 private:
  AppSettings settings_;
  FixedTimestep timestep_;
  FrameStats stats_;
  bool quit_ = false;
  int exit_code_ = 0;
  uint64_t frames_ = 0;
};

}  // namespace pb
