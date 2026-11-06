// PAWBOUND dedicated server — headless, authoritative.
#include <atomic>
#include <charconv>
#include <cstdlib>
#include <csignal>
#include <iostream>
#include <string_view>
#include <thread>

#include "core/log.hpp"
#include "core/version.hpp"
#include "server/game_server.hpp"

namespace {
std::atomic<bool> g_stop{false};
extern "C" void on_signal(int) { g_stop.store(true); }

template <class T>
bool num(std::string_view s, T& out) {
  return std::from_chars(s.data(), s.data() + s.size(), out).ec == std::errc{};
}
bool fnum(std::string_view s, double& out) {
  try {
    out = std::stod(std::string(s));
    return true;
  } catch (...) {
    return false;
  }
}
}  // namespace

int main(int argc, char** argv) {
  pb::server::ServerConfig cfg;
  bool console = true;
  for (int i = 1; i < argc; ++i) {
    std::string_view s = argv[i];
    auto next = [&]() -> std::string_view { return i + 1 < argc ? std::string_view(argv[++i]) : std::string_view(); };
    bool ok = true;
    if (s == "--ticks") ok = num(next(), cfg.max_ticks);
    else if (s == "--port") ok = num(next(), cfg.port);
    else if (s == "--max-players") ok = num(next(), cfg.max_players);
    else if (s == "--server-id") cfg.server_id = next();
    else if (s == "--world") cfg.world_dir = std::string(next());
    else if (s == "--data") cfg.data_root = std::string(next());
    else if (s == "--ticket-pubkey") cfg.ticket_pubkey = std::string(next());
    else if (s == "--metrics") cfg.metrics_file = std::string(next());
    else if (s == "--metrics-interval") ok = fnum(next(), cfg.metrics_interval_s);
    else if (s == "--sim-loss") ok = fnum(next(), cfg.netsim.loss);
    else if (s == "--sim-latency") ok = fnum(next(), cfg.netsim.latency_ms);
    else if (s == "--sim-jitter") ok = fnum(next(), cfg.netsim.jitter_ms);
    else if (s == "--backend") cfg.backend_url = next();
    else if (s == "--region") cfg.region = next();
    else if (s == "--public-address") cfg.public_address = next();
    else if (s == "--autosave") ok = fnum(next(), cfg.autosave_interval_s) && cfg.autosave_interval_s >= 1;
    else if (s == "--linger") ok = fnum(next(), cfg.linger_s);
    else if (s == "--no-console") console = false;
    else ok = false;
    if (!ok) {
      pb::log::error("server", "tham số không hợp lệ: {}", s);
      pb::log::error("server", "cách dùng: pawbound_server [--world dir] [--data root] [--port n] [--server-id id] "
                               "[--ticket-pubkey file] [--max-players n] [--metrics file.jsonl] [--ticks n] "
                               "[--sim-loss 0..1 --sim-latency ms --sim-jitter ms] [--no-console] "
                               "[--backend http://host:3000/api/v1 --region vn --public-address ip --autosave s --linger s]");
      return 2;
    }
  }
  // Bí mật chỉ qua biến môi trường (không lộ trong danh sách tiến trình / lịch sử shell)
  if (const char* t = std::getenv("PAWBOUND_SERVICE_TOKEN")) cfg.service_token = t;
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);
  pb::log::info("server", "PAWBOUND server v{} (protocol {})", pb::kVersionString, pb::net::kProtocolVersion);

  pb::server::GameServer server(cfg);
  std::string err;
  if (!server.init(err)) {
    pb::log::fatal("server", "{}", err);
    return 1;
  }
  // Console: thread chỉ đọc stdin và đẩy lệnh vào hàng đợi của server.
  if (console) {
    std::thread([&] {
      std::string line;
      while (!g_stop.load() && std::getline(std::cin, line)) {
        if (line == "quit") {
          g_stop.store(true);
          break;
        }
        if (!g_stop.load()) server.post_command(line);
      }
    }).detach();
  }
  std::atomic<bool> stop{false};
  std::thread watcher([&] {
    while (!stop.load()) {
      if (g_stop.load()) stop.store(true);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  });
  const int code = server.run(stop);
  g_stop.store(true);  // chặn thread console gọi vào server trước khi server bị hủy
  stop.store(true);
  watcher.join();
  return code;
}
