#pragma once

#include <algorithm>
#include <atomic>
#include <future>
#include <stdexcept>
#include <thread>

namespace gagp {

// Host worker threads for the CPU engine and the host-side reproduction
// stages. Set once before a run (`gagp run --threads N`); the default is one
// thread. CUDA driver service threads are outside this policy. Results do not
// depend on the thread count.
inline constexpr unsigned kMaxHostThreads = 64;

inline std::atomic<unsigned>& host_thread_setting() {
  static std::atomic<unsigned> value{1};
  return value;
}

inline void set_host_thread_limit(unsigned count) {
  if (count < 1 || count > kMaxHostThreads)
    throw std::invalid_argument("host thread count must be in [1,64]");
  host_thread_setting().store(count);
}

inline unsigned host_thread_limit() {
  return std::min(host_thread_setting().load(), std::max(1u, std::thread::hardware_concurrency()));
}

inline std::launch host_launch_policy() {
  return host_thread_limit() > 1 ? std::launch::async : std::launch::deferred;
}

}  // namespace gagp
