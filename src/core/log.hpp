#pragma once
// Logger tối giản, thread-safe: level, timestamp UTC, category, console + file.
#include <chrono>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace pb::log {

enum class Level { Trace = 0, Debug, Info, Warn, Error, Fatal, Off };

void set_level(Level level);
[[nodiscard]] Level level();
[[nodiscard]] bool enabled(Level level);
[[nodiscard]] std::optional<Level> parse_level(std::string_view text);
[[nodiscard]] std::string_view level_name(Level level);

// Mở file log (append), tạo thư mục cha nếu cần. Trả false nếu không mở được.
bool open_file(const std::filesystem::path& path);
void close_file();
void set_console(bool enabled);

// "2026-11-06T10:20:30.123Z [INFO ] [core] message"
[[nodiscard]] std::string format_line(Level level, std::string_view category, std::string_view message,
                                      std::chrono::system_clock::time_point when);

void write(Level level, std::string_view category, std::string_view message);

template <class... Args>
void logf(Level lv, std::string_view cat, std::format_string<Args...> fmt, Args&&... args) {
  if (!enabled(lv)) return;
  write(lv, cat, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void trace(std::string_view c, std::format_string<Args...> f, Args&&... a) {
  logf(Level::Trace, c, f, std::forward<Args>(a)...);
}
template <class... Args>
void debug(std::string_view c, std::format_string<Args...> f, Args&&... a) {
  logf(Level::Debug, c, f, std::forward<Args>(a)...);
}
template <class... Args>
void info(std::string_view c, std::format_string<Args...> f, Args&&... a) {
  logf(Level::Info, c, f, std::forward<Args>(a)...);
}
template <class... Args>
void warn(std::string_view c, std::format_string<Args...> f, Args&&... a) {
  logf(Level::Warn, c, f, std::forward<Args>(a)...);
}
template <class... Args>
void error(std::string_view c, std::format_string<Args...> f, Args&&... a) {
  logf(Level::Error, c, f, std::forward<Args>(a)...);
}
template <class... Args>
void fatal(std::string_view c, std::format_string<Args...> f, Args&&... a) {
  logf(Level::Fatal, c, f, std::forward<Args>(a)...);
}

}  // namespace pb::log
