#include <cassert>
#include <atomic>
#include <cstdlib>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include "gagp/core/host_threads.hpp"
#include "../../src/evolution/batch_workers.hpp"

int main() {
  const char* old = std::getenv("GAGP_HOST_THREADS");
  const std::string saved = old ? old : "";
  const bool present = old != nullptr;
  unsetenv("GAGP_HOST_THREADS");
  assert(gagp::host_thread_limit() == 1);
  const auto caller = std::this_thread::get_id();
  for (const char* setting : {"1", "2", "20"}) {
    setenv("GAGP_HOST_THREADS", setting, 1);
    std::atomic<unsigned> calls{0};
    gagp::evo::detail::BatchWorkers workers(20);
    workers.run([&] {
      if (*setting == '1') assert(std::this_thread::get_id() == caller);
      ++calls;
    });
    assert(calls == gagp::host_thread_limit());
    try { workers.run([] { throw std::runtime_error("worker error"); }); assert(false); }
    catch (const std::runtime_error&) {}
  }
  setenv("GAGP_HOST_THREADS", "1", 1);
  bool called = false;
  auto deferred = std::async(gagp::host_launch_policy(), [&] {
    assert(std::this_thread::get_id() == caller); called = true;
  });
  assert(!called);
  deferred.wait();
  assert(called);
  deferred.get();
  for (const char* setting : {"", "0", "-1", "21", "2x", "999999999999"}) {
    setenv("GAGP_HOST_THREADS", setting, 1);
    try { (void)gagp::host_thread_limit(); assert(false); }
    catch (const std::invalid_argument&) {}
  }
  if (present) setenv("GAGP_HOST_THREADS", saved.c_str(), 1);
  else unsetenv("GAGP_HOST_THREADS");
}
