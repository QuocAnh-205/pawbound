#pragma once
// Kênh sự kiện có kiểu (không có global event bus).
// - Subscription là RAII: hủy subscription = unsubscribe. An toàn khi kênh đã bị hủy trước (weak_ptr).
// - Unsubscribe trong lúc dispatch: handler bị bỏ qua ngay, dọn dẹp sau khi dispatch xong.
// - Subscribe trong lúc dispatch: handler mới chỉ nhận từ lần publish sau.
// - Chỉ dùng trên một thread (main/simulation thread).
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace pb {

template <class E>
class EventChannel {
 public:
  using Handler = std::function<void(const E&)>;

 private:
  struct State {
    struct Entry {
      uint64_t id;
      Handler fn;
    };
    std::vector<Entry> handlers;
    uint64_t next_id = 1;
    int dispatch_depth = 0;
    bool dirty = false;
    void unsubscribe(uint64_t id) {
      for (auto it = handlers.begin(); it != handlers.end(); ++it) {
        if (it->id != id) continue;
        if (dispatch_depth > 0) {
          it->fn = nullptr;
          dirty = true;
        } else {
          handlers.erase(it);
        }
        return;
      }
    }
    void compact() {
      if (!dirty || dispatch_depth > 0) return;
      std::erase_if(handlers, [](const Entry& e) { return !e.fn; });
      dirty = false;
    }
  };

 public:
  class Subscription {
   public:
    Subscription() = default;
    Subscription(std::weak_ptr<State> s, uint64_t id) : state_(std::move(s)), id_(id) {}
    Subscription(Subscription&& o) noexcept : state_(std::move(o.state_)), id_(std::exchange(o.id_, 0)) {}
    Subscription& operator=(Subscription&& o) noexcept {
      if (this != &o) {
        reset();
        state_ = std::move(o.state_);
        id_ = std::exchange(o.id_, 0);
      }
      return *this;
    }
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    ~Subscription() { reset(); }
    void reset() {
      if (id_ == 0) return;
      if (auto s = state_.lock()) s->unsubscribe(id_);
      id_ = 0;
      state_.reset();
    }
    [[nodiscard]] bool active() const { return id_ != 0 && !state_.expired(); }

   private:
    std::weak_ptr<State> state_;
    uint64_t id_ = 0;
  };

  [[nodiscard]] Subscription subscribe(Handler fn) {
    const uint64_t id = state_->next_id++;
    state_->handlers.push_back({id, std::move(fn)});
    return Subscription(state_, id);
  }

  // Gửi ngay lập tức tới các subscriber hiện có.
  void publish(const E& event) {
    auto keep = state_;  // giữ state sống nếu handler hủy channel (không khuyến khích)
    const size_t n = keep->handlers.size();
    ++keep->dispatch_depth;
    for (size_t i = 0; i < n && i < keep->handlers.size(); ++i) {
      if (keep->handlers[i].fn) {
        auto fn = keep->handlers[i].fn;  // copy: vector có thể realloc khi handler subscribe thêm
        fn(event);
      }
    }
    --keep->dispatch_depth;
    keep->compact();
  }

  // Hàng đợi: enqueue trong frame, flush tại điểm xác định của vòng lặp.
  void enqueue(E event) { queue_.push_back(std::move(event)); }
  void flush() {
    std::vector<E> pending;
    pending.swap(queue_);
    for (const auto& e : pending) publish(e);
  }

  [[nodiscard]] size_t subscriber_count() const {
    size_t n = 0;
    for (const auto& h : state_->handlers)
      if (h.fn) ++n;
    return n;
  }
  [[nodiscard]] size_t queued() const { return queue_.size(); }

 private:
  std::shared_ptr<State> state_ = std::make_shared<State>();
  std::vector<E> queue_;
};

}  // namespace pb
