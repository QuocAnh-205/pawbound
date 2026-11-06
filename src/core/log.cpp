#include "core/log.hpp"

#include <atomic>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <mutex>

namespace pb::log {
namespace {
std::atomic<Level> g_level{Level::Info};
std::atomic<bool> g_console{true};
std::mutex g_mutex;
std::ofstream g_file;
}  // namespace

void set_level(Level lv) { g_level.store(lv); }
Level level() { return g_level.load(); }
bool enabled(Level lv) { return lv != Level::Off && lv >= g_level.load(); }
void set_console(bool on) { g_console.store(on); }

std::string_view level_name(Level lv) {
  switch (lv) {
    case Level::Trace: return "TRACE";
    case Level::Debug: return "DEBUG";
    case Level::Info: return "INFO ";
    case Level::Warn: return "WARN ";
    case Level::Error: return "ERROR";
    case Level::Fatal: return "FATAL";
    case Level::Off: return "OFF  ";
  }
  return "?????";
}

std::optional<Level> parse_level(std::string_view text) {
  std::string s;
  for (char c : text) s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  if (s == "trace") return Level::Trace;
  if (s == "debug") return Level::Debug;
  if (s == "info") return Level::Info;
  if (s == "warn" || s == "warning") return Level::Warn;
  if (s == "error") return Level::Error;
  if (s == "fatal") return Level::Fatal;
  if (s == "off") return Level::Off;
  return std::nullopt;
}

bool open_file(const std::filesystem::path& path) {
  std::scoped_lock lock(g_mutex);
  if (g_file.is_open()) g_file.close();
  std::error_code ec;
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
  g_file.open(path, std::ios::out | std::ios::app);
  return g_file.is_open();
}

void close_file() {
  std::scoped_lock lock(g_mutex);
  if (g_file.is_open()) g_file.close();
}

std::string format_line(Level lv, std::string_view category, std::string_view message,
                        std::chrono::system_clock::time_point when) {
  const auto ms = std::chrono::time_point_cast<std::chrono::milliseconds>(when);
  return std::format("{:%Y-%m-%dT%H:%M:%S}Z [{}] [{}] {}", ms, level_name(lv), category, message);
}

void write(Level lv, std::string_view category, std::string_view message) {
  if (!enabled(lv)) return;
  const std::string line = format_line(lv, category, message, std::chrono::system_clock::now());
  std::scoped_lock lock(g_mutex);
  if (g_console.load()) {
    std::FILE* out = lv >= Level::Warn ? stderr : stdout;
    std::fputs(line.c_str(), out);
    std::fputc('\n', out);
    std::fflush(out);
  }
  if (g_file.is_open()) {
    g_file << line << '\n';
    if (lv >= Level::Warn) g_file.flush();
  }
}

}  // namespace pb::log
