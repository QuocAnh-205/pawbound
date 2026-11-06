#include "server/backend_link.hpp"

#include "core/log.hpp"

namespace pb::server {

BackendLink::~BackendLink() { stop(std::chrono::milliseconds(0)); }

bool BackendLink::start(const std::string& url, std::string service_token, std::string& error) {
  if (service_token.size() < 32) {
    error = "service token thiếu hoặc quá ngắn (đặt biến môi trường PAWBOUND_SERVICE_TOKEN, >= 32 ký tự)";
    return false;
  }
  if (!net::parse_http_url(url, url_, error)) return false;
  token_ = std::move(service_token);
  quit_ = false;
  running_ = true;
  th_ = std::thread([this] { worker(); });
  return true;
}

void BackendLink::stop(std::chrono::milliseconds drain_timeout) {
  if (!running_) return;
  {
    std::unique_lock l(mu_);
    cv_.wait_for(l, drain_timeout, [&] { return jobs_.empty() && in_flight_ == 0; });
    if (!jobs_.empty()) log::warn("backend", "dừng khi còn {} job chưa gửi", jobs_.size());
    quit_ = true;
    jobs_.clear();
  }
  cv_.notify_all();
  if (th_.joinable()) th_.join();
  running_ = false;
}

void BackendLink::submit(BackendRequest r) {
  if (!running_) return;
  {
    std::scoped_lock l(mu_);
    jobs_.push_back(std::move(r));
  }
  cv_.notify_all();
}

std::deque<BackendResult> BackendLink::poll() {
  std::deque<BackendResult> out;
  std::scoped_lock l(mu_);
  out.swap(results_);
  return out;
}

size_t BackendLink::pending() const {
  std::scoped_lock l(mu_);
  return jobs_.size() + in_flight_;
}

void BackendLink::worker() {
  for (;;) {
    BackendRequest r;
    {
      std::unique_lock l(mu_);
      cv_.wait(l, [&] { return quit_ || !jobs_.empty(); });
      if (quit_) return;
      r = std::move(jobs_.front());
      jobs_.pop_front();
      ++in_flight_;
    }
    BackendResult res;
    res.kind = r.kind;
    res.player = r.player;
    res.char_id = r.char_id;
    // Lỗi mạng / 5xx: thử lại tối đa 3 lần (Save/EndSession idempotent nên gửi lại an toàn)
    for (int attempt = 0; attempt < 3; ++attempt) {
      auto h = net::http_request(url_, r.method, r.path, {{"X-Service-Token", token_}}, r.body);
      res.status = h.status;
      res.body = std::move(h.body);
      res.error = std::move(h.error);
      if (res.status != 0 && res.status < 500) break;
      if (r.kind == BackendJob::Heartbeat) break;  // heartbeat sau sẽ tự gửi lại
      std::unique_lock l(mu_);
      if (cv_.wait_for(l, std::chrono::milliseconds(300 * (attempt + 1)), [&] { return quit_; })) break;
    }
    {
      std::scoped_lock l(mu_);
      results_.push_back(std::move(res));
      --in_flight_;
    }
    cv_.notify_all();
  }
}

}  // namespace pb::server
