#pragma once
// HTTP/1.1 client tối giản, BLOCKING (gọi trên thread nền) — chỉ cho API NỘI BỘ game server -> Backend
// trong mạng riêng (http://). Production: đặt game server và backend cùng mạng nội bộ hoặc qua sidecar TLS.
#include <chrono>
#include <map>
#include <string>

namespace pb::net {

struct HttpUrl {
  std::string host;
  uint16_t port = 80;
  std::string base_path;  // vd "/api/v1"
};
bool parse_http_url(const std::string& url, HttpUrl& out, std::string& error);

struct HttpResponse {
  int status = 0;  // 0 = lỗi mạng
  std::string body;
  std::string error;
};

HttpResponse http_request(const HttpUrl& base, const std::string& method, const std::string& path,
                          const std::map<std::string, std::string>& headers, const std::string& body,
                          std::chrono::milliseconds timeout = std::chrono::milliseconds(3000));

}  // namespace pb::net
