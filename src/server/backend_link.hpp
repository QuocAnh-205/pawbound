#pragma once
// Kết nối game server -> Backend (API nội bộ /internal, header X-Service-Token).
// Mọi HTTP chạy trên MỘT thread nền; vòng lặp game chỉ đẩy job và lấy kết quả (poll) -> tick không bao giờ bị chặn.
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "net/http_client.hpp"

namespace pb::server {

enum class BackendJob : uint8_t { Heartbeat, Load, Save, EndSession, Social };

struct BackendRequest {
  BackendJob kind = BackendJob::Heartbeat;
  uint32_t player = 0;
  uint64_t char_id = 0;
  std::string method, path, body;
};

struct BackendResult {
  BackendJob kind = BackendJob::Heartbeat;
  uint32_t player = 0;
  uint64_t char_id = 0;
  int status = 0;  // 0 = lỗi mạng
  std::string body, error;
};

class BackendLink {
 public:
  ~BackendLink();
  // url ví dụ "http://127.0.0.1:3000/api/v1"; token đọc từ biến môi trường (KHÔNG truyền qua dòng lệnh).
  bool start(const std::string& url, std::string service_token, std::string& error);
  // Chờ xử lý hết job còn lại (tối đa timeout) rồi dừng thread.
  void stop(std::chrono::milliseconds drain_timeout = std::chrono::milliseconds(8000));
  [[nodiscard]] bool enabled() const { return running_; }
  void submit(BackendRequest r);
  std::deque<BackendResult> poll();
  [[nodiscard]] size_t pending() const;

 private:
  void worker();
  net::HttpUrl url_;
  std::string token_;
  std::thread th_;
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<BackendRequest> jobs_;
  std::deque<BackendResult> results_;
  size_t in_flight_ = 0;
  bool running_ = false, quit_ = false;
};

}  // namespace pb::server
