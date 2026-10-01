#pragma once

#include "gagp/core/host_threads.hpp"
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace gagp::evo::detail {

// One operation owns this team. run() is a barrier: all readers finish before
// the caller publishes a batch or releases any referenced inputs. No state or
// thread-local payload scope survives an individual callback.
class BatchWorkers {
 public:
  explicit BatchWorkers(std::size_t count) {
    count = std::min<std::size_t>(count, gagp::host_thread_limit());
    if (count <= 1) return;
    errors_.resize(count);
    try {
      for (std::size_t i = 0; i < count; ++i)
        threads_.emplace_back([this, i] { worker(i); });
    } catch (...) {
      stop();
      throw;
    }
  }
  ~BatchWorkers() { stop(); }
  BatchWorkers(const BatchWorkers&) = delete;
  BatchWorkers& operator=(const BatchWorkers&) = delete;

  void run(std::function<void()> action) {
    if (threads_.empty()) { action(); return; }
    std::unique_lock<std::mutex> lock(mutex_);
    action_ = std::move(action);
    for (auto& error : errors_) error = nullptr;
    remaining_ = threads_.size();
    ++epoch_;
    ready_.notify_all();
    done_.wait(lock, [this] { return remaining_ == 0; });
    action_ = {};
    for (const auto& error : errors_) if (error) std::rethrow_exception(error);
  }

 private:
  void stop() noexcept {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
    }
    ready_.notify_all();
    for (auto& thread : threads_) thread.join();
  }
  void worker(std::size_t index) {
    std::size_t epoch = 0;
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
      ready_.wait(lock, [&] { return stopping_ || epoch_ != epoch; });
      if (stopping_) return;
      epoch = epoch_;
      lock.unlock();
      try { action_(); } catch (...) { errors_[index] = std::current_exception(); }
      lock.lock();
      if (--remaining_ == 0) done_.notify_one();
    }
  }
  std::mutex mutex_;
  std::condition_variable ready_, done_;
  std::vector<std::thread> threads_;
  std::vector<std::exception_ptr> errors_;
  std::function<void()> action_;
  std::size_t epoch_ = 0, remaining_ = 0;
  bool stopping_ = false;
};

}  // namespace gagp::evo::detail
