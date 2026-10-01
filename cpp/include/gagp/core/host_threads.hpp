#pragma once

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <future>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace gagp {

// Process configuration, fixed for the duration of a run. Application host
// parallelism is opt-in; CUDA driver service threads are outside this policy.
inline unsigned host_thread_limit() {
  const char* setting = std::getenv("GAGP_HOST_THREADS");
  if (!setting) return 1;
  const std::string_view text(setting);
  unsigned count = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), count);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
      count < 1 || count > 20)
    throw std::invalid_argument("GAGP_HOST_THREADS must be an integer in [1,20]");
  return std::min(count, std::max(1u, std::thread::hardware_concurrency()));
}

inline std::launch host_launch_policy() {
  return host_thread_limit() > 1 ? std::launch::async : std::launch::deferred;
}

}  // namespace gagp
