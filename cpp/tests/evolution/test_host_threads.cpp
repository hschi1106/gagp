#include <cassert>
#include <atomic>
#include <future>
#include <stdexcept>
#include <thread>
#include "gagp/core/host_threads.hpp"
#include "../../src/evolution/batch_workers.hpp"

int main() {
  assert(gagp::host_thread_limit() == 1);
  const auto caller = std::this_thread::get_id();
  for (unsigned setting : {1u, 2u, 20u}) {
    gagp::set_host_thread_limit(setting);
    std::atomic<unsigned> calls{0};
    gagp::evo::detail::BatchWorkers workers(20);
    workers.run([&] {
      if (setting == 1) assert(std::this_thread::get_id() == caller);
      ++calls;
    });
    assert(calls == gagp::host_thread_limit());
    try { workers.run([] { throw std::runtime_error("worker error"); }); assert(false); }
    catch (const std::runtime_error&) {}
  }
  gagp::set_host_thread_limit(1);
  bool called = false;
  auto deferred = std::async(gagp::host_launch_policy(), [&] {
    assert(std::this_thread::get_id() == caller); called = true;
  });
  assert(!called);
  deferred.wait();
  assert(called);
  deferred.get();
  for (unsigned setting : {0u, 65u}) {
    try { gagp::set_host_thread_limit(setting); assert(false); }
    catch (const std::invalid_argument&) {}
  }
}
