#include "net/http_client.hpp"

#include <cstring>
#include <format>
#include <string_view>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static void close_socket(socket_t s) { closesocket(s); }
static bool bad_socket(socket_t s) { return s == INVALID_SOCKET; }
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using socket_t = int;
static void close_socket(socket_t s) { close(s); }
static bool bad_socket(socket_t s) { return s < 0; }
#endif

namespace pb::net {

bool parse_http_url(const std::string& url, HttpUrl& out, std::string& error) {
  constexpr std::string_view scheme = "http://";
  if (url.rfind(scheme, 0) != 0) {
    error = "chỉ hỗ trợ http:// (API nội bộ)";
    return false;
  }
  std::string rest = url.substr(scheme.size());
  const auto slash = rest.find('/');
  std::string hostport = rest.substr(0, slash);
  out.base_path = slash == std::string::npos ? "" : rest.substr(slash);
  while (!out.base_path.empty() && out.base_path.back() == '/') out.base_path.pop_back();
  const auto colon = hostport.rfind(':');
  out.host = hostport.substr(0, colon);
  out.port = 80;
  if (colon != std::string::npos) {
    try {
      const int p = std::stoi(hostport.substr(colon + 1));
      if (p < 1 || p > 65535) throw std::out_of_range("port");
      out.port = static_cast<uint16_t>(p);
    } catch (...) {
      error = "port không hợp lệ";
      return false;
    }
  }
  if (out.host.empty()) {
    error = "thiếu host";
    return false;
  }
  return true;
}

HttpResponse http_request(const HttpUrl& base, const std::string& method, const std::string& path,
                          const std::map<std::string, std::string>& headers, const std::string& body,
                          std::chrono::milliseconds timeout) {
  HttpResponse res;
#if defined(_WIN32)
  static const bool wsa_ok = [] {
    WSADATA d;
    return WSAStartup(MAKEWORD(2, 2), &d) == 0;
  }();
  if (!wsa_ok) {
    res.error = "WSAStartup thất bại";
    return res;
  }
#endif
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* ai = nullptr;
  if (getaddrinfo(base.host.c_str(), std::to_string(base.port).c_str(), &hints, &ai) != 0 || !ai) {
    res.error = "không phân giải được " + base.host;
    return res;
  }
  socket_t s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
  if (bad_socket(s)) {
    freeaddrinfo(ai);
    res.error = "socket() thất bại";
    return res;
  }
#if defined(_WIN32)
  const DWORD tv = static_cast<DWORD>(timeout.count());
#else
  timeval tv{static_cast<time_t>(timeout.count() / 1000), static_cast<suseconds_t>((timeout.count() % 1000) * 1000)};
#endif
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
  const int rc = connect(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen));
  freeaddrinfo(ai);
  if (rc != 0) {
    close_socket(s);
    res.error = std::format("không kết nối được {}:{}", base.host, base.port);
    return res;
  }
  std::string req = std::format("{} {}{} HTTP/1.1\r\nHost: {}:{}\r\nConnection: close\r\nContent-Length: {}\r\n", method,
                                base.base_path, path, base.host, base.port, body.size());
  for (const auto& [k, v] : headers) req += k + ": " + v + "\r\n";
  if (!body.empty()) req += "Content-Type: application/json\r\n";
  req += "\r\n" + body;
  for (size_t sent = 0; sent < req.size();) {
    const int n = send(s, req.data() + sent, static_cast<int>(req.size() - sent), 0);
    if (n <= 0) {
      close_socket(s);
      res.error = "gửi thất bại";
      return res;
    }
    sent += static_cast<size_t>(n);
  }
  std::string raw;
  char buf[8192];
  for (;;) {
    const int n = recv(s, buf, sizeof(buf), 0);
    if (n <= 0) break;  // đóng kết nối (Connection: close) hoặc timeout
    raw.append(buf, static_cast<size_t>(n));
    if (raw.size() > (1u << 20)) break;  // giới hạn 1 MiB
  }
  close_socket(s);
  const auto head_end = raw.find("\r\n\r\n");
  if (raw.rfind("HTTP/1.", 0) != 0 || head_end == std::string::npos) {
    res.error = "phản hồi HTTP không hợp lệ";
    return res;
  }
  res.status = std::atoi(raw.c_str() + 9);
  std::string head = raw.substr(0, head_end);
  res.body = raw.substr(head_end + 4);
  // Bỏ mã hóa chunked nếu backend dùng (Express mặc định gửi Content-Length cho JSON)
  for (auto& c : head) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (head.find("transfer-encoding: chunked") != std::string::npos) {
    std::string out;
    size_t p = 0;
    while (p < res.body.size()) {
      const auto eol = res.body.find("\r\n", p);
      if (eol == std::string::npos) break;
      const size_t len = std::strtoul(res.body.substr(p, eol - p).c_str(), nullptr, 16);
      if (len == 0) break;
      out += res.body.substr(eol + 2, len);
      p = eol + 2 + len + 2;
    }
    res.body = out;
  }
  return res;
}

}  // namespace pb::net
