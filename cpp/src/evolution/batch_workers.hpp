#pragma once

#include <cstddef>
#include <functional>

namespace gagp::evo::detail {

// Serial stand-in for the retired host worker team: the host is always one
// thread. The native-AST preparation code that still calls it is transitional.
class BatchWorkers {
 public:
  explicit BatchWorkers(std::size_t) {}
  void run(const std::function<void()>& action) { action(); }
};

}  // namespace gagp::evo::detail
