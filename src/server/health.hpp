#pragma once
// HTTP tối giản cho vận hành: GET /health (200 khi vòng lặp game còn chạy) và GET /metrics (Prometheus text).
// Thread riêng; vòng lặp game chỉ cập nhật snapshot dưới mutex. Chỉ đọc — không có endpoint thay đổi trạng thái.
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace pb::server {

class HealthServer {
 public:
  ~HealthServer();
  bool start(const std::string& bind_addr, uint16_t port, std::string& error);
  void stop();
  // Gọi từ vòng lặp game (mỗi giây): nội dung /metrics + thời điểm "nhịp tim" của vòng lặp.
  void publish(std::string metrics_text, double loop_time_s);
  [[nodiscard]] uint16_t port() const { return port_; }

 private:
  void run();
  std::string handle(const std::string& request_line);
  std::thread th_;
  std::atomic<bool> quit_{false};
  intptr_t sock_ = -1;
  uint16_t port_ = 0;
  std::mutex mu_;
  std::string metrics_;
  double last_beat_ = -1;  // giây (đồng hồ đơn điệu) lần publish gần nhất
};

}  // namespace pb::server
