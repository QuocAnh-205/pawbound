#include "server/health.hpp"

#include <chrono>
#include <cstring>
#include <format>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
static void sclose(sock_t s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
static void sclose(sock_t s) { close(s); }
#endif

namespace pb::server {
namespace {
double mono_s() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}  // namespace

HealthServer::~HealthServer() { stop(); }

bool HealthServer::start(const std::string& bind_addr, uint16_t port, std::string& error) {
#if defined(_WIN32)
  WSADATA d;
  WSAStartup(MAKEWORD(2, 2), &d);
#endif
  sock_t s = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  if (inet_pton(AF_INET, bind_addr.c_str(), &a.sin_addr) != 1) {
    error = "địa chỉ health không hợp lệ: " + bind_addr;
    sclose(s);
    return false;
  }
  if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || listen(s, 16) != 0) {
    error = std::format("không mở được cổng health TCP {}:{}", bind_addr, port);
    sclose(s);
    return false;
  }
  sock_ = static_cast<intptr_t>(s);
  port_ = port;
  th_ = std::thread([this] { run(); });
  return true;
}

void HealthServer::stop() {
  if (!th_.joinable()) return;
  quit_ = true;
  th_.join();
  sclose(static_cast<sock_t>(sock_));
}

void HealthServer::publish(std::string metrics_text, double /*loop_time_s*/) {
  std::scoped_lock l(mu_);
  metrics_ = std::move(metrics_text);
  last_beat_ = mono_s();
}

std::string HealthServer::handle(const std::string& line) {
  std::string body, status = "200 OK", type = "text/plain; version=0.0.4";
  double beat;
  {
    std::scoped_lock l(mu_);
    body = metrics_;
    beat = last_beat_;
  }
  const bool alive = beat >= 0 && mono_s() - beat < 10.0;  // vòng lặp game không cập nhật > 10 s = treo
  if (line.rfind("GET /health", 0) == 0) {
    type = "application/json";
    body = alive ? R"({"status":"ok"})" : R"({"status":"stalled"})";
    if (!alive) status = "503 Service Unavailable";
  } else if (line.rfind("GET /metrics", 0) != 0) {
    status = "404 Not Found";
    body = "not found\n";
  }
  return std::format("HTTP/1.1 {}\r\nContent-Type: {}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}", status, type, body.size(), body);
}

void HealthServer::run() {
  const auto ls = static_cast<sock_t>(sock_);
  while (!quit_) {
    fd_set rf;
    FD_ZERO(&rf);
    FD_SET(ls, &rf);
    timeval tv{0, 200000};
    if (select(static_cast<int>(ls + 1), &rf, nullptr, nullptr, &tv) <= 0) continue;
    sock_t c = accept(ls, nullptr, nullptr);
#if defined(_WIN32)
    if (c == INVALID_SOCKET) continue;
    const DWORD to = 2000;
#else
    if (c < 0) continue;
    const timeval to{2, 0};
#endif
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&to), sizeof(to));
    char buf[1024];
    const int n = recv(c, buf, sizeof(buf) - 1, 0);  // chỉ cần dòng đầu; giới hạn 1 KiB
    if (n > 0) {
      buf[n] = 0;
      std::string req(buf);
      const auto eol = req.find("\r\n");
      const std::string out = handle(req.substr(0, eol));
      send(c, out.data(), static_cast<int>(out.size()), 0);
    }
    sclose(c);
  }
}

}  // namespace pb::server
