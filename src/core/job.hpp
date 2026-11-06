#pragma once
// Thread pool tối thiểu cho việc nặng, KHÔNG đụng vào state gameplay:
// tải file, giải nén, sinh dữ liệu chunk, tìm đường... Kết quả trả qua std::future
// và được áp vào ECS/world trên main/simulation thread.
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace pb {

class ThreadPool {
 public:
  explicit ThreadPool(unsigned threads = 0);
  ~ThreadPool();
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  template <class F>
  auto submit(F&& fn) -> std::future<std::invoke_result_t<F>> {
    using R = std::invoke_result_t<F>;
    auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(fn));
    auto fut = task->get_future();
    {
      std::scoped_lock lock(mutex_);
      if (stopping_) throw std::runtime_error("ThreadPool đã dừng");
      queue_.emplace([task] { (*task)(); });
    }
    cv_.notify_one();
    return fut;
  }
  [[nodiscard]] size_t thread_count() const { return workers_.size(); }

 private:
  void worker_loop();
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> queue_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool stopping_ = false;
};

}  // namespace pb
