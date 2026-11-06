#include "core/job.hpp"

#include <algorithm>

namespace pb {

ThreadPool::ThreadPool(unsigned threads) {
  if (threads == 0) threads = std::max(1u, std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1u);
  workers_.reserve(threads);
  for (unsigned i = 0; i < threads; ++i) workers_.emplace_back([this] { worker_loop(); });
}

ThreadPool::~ThreadPool() {
  {
    std::scoped_lock lock(mutex_);
    stopping_ = true;
  }
  cv_.notify_all();
  for (auto& t : workers_) t.join();  // các job còn trong hàng đợi vẫn được chạy hết
}

void ThreadPool::worker_loop() {
  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (queue_.empty()) return;  // stopping_ và hết việc
      job = std::move(queue_.front());
      queue_.pop();
    }
    job();  // packaged_task tự bắt exception vào future
  }
}

}  // namespace pb
