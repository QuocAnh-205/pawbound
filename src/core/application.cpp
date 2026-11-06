#include "core/application.hpp"

#include <chrono>
#include <exception>

#include "core/log.hpp"

namespace pb {

Application::Application(AppSettings settings)
    : settings_(settings),
      timestep_(1.0 / settings.fixed_hz, settings.max_steps_per_frame, settings.max_frame_delta),
      stats_(settings.stats_interval > 0 ? settings.stats_interval : 1e30) {}

void Application::request_quit(int exit_code) {
  quit_ = true;
  exit_code_ = exit_code;
}

double Application::now_seconds() {
  using clock = std::chrono::steady_clock;
  static const auto start = clock::now();
  return std::chrono::duration<double>(clock::now() - start).count();
}

void Application::on_stats(const FrameStats::Report& r) {
  log::info(settings_.log_category, "fps={:.1f} frame avg={:.2f}ms max={:.2f}ms ticks={} ({:.1f}/s)", r.fps,
            r.avg_ms, r.max_ms, r.ticks, static_cast<double>(r.ticks) * r.fps / static_cast<double>(r.frames));
}

int Application::run() {
  quit_ = false;
  exit_code_ = 0;
  bool initialized = false;
  try {
    if (!on_init()) {
      log::error(settings_.log_category, "khởi tạo thất bại");
      on_shutdown();
      return 1;
    }
    initialized = true;
    double last = now_seconds();
    while (!quit_) {
      const double now = now_seconds();
      const double dt = now - last;
      last = now;
      on_frame_begin();
      if (quit_) break;
      const int steps = timestep_.advance(dt);
      for (int i = 0; i < steps && !quit_; ++i) on_fixed_update(timestep_.step());
      on_update(dt);
      on_render(timestep_.alpha());
      ++frames_;
      if (auto rep = stats_.add_frame(dt, steps)) on_stats(*rep);
    }
  } catch (const std::exception& e) {
    log::fatal(settings_.log_category, "exception không được xử lý: {}", e.what());
    exit_code_ = 1;
  } catch (...) {
    log::fatal(settings_.log_category, "exception không xác định");
    exit_code_ = 1;
  }
  try {
    if (initialized) on_shutdown();
  } catch (const std::exception& e) {
    log::fatal(settings_.log_category, "exception khi shutdown: {}", e.what());
    exit_code_ = 1;
  }
  return exit_code_;
}

}  // namespace pb
