#pragma once

#include <cstdint>

namespace gagp::evo::repro {

// Device counterpart of grammar::GrammarRandom. Keep the state transition and
// rejection policy byte-for-byte equivalent so compiled-grammar reproduction
// can replay host-generated mutation decisions from the same seed.
class DGrammarRandom {
 public:
  __device__ explicit DGrammarRandom(std::uint64_t seed) : state_(seed) {}

  __device__ std::uint64_t next() {
    std::uint64_t value =
        (state_ += UINT64_C(0x9e3779b97f4a7c15));
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
  }

  // Callers must provide a positive bound, matching GrammarRandom::bounded's
  // successful-call precondition. Device code cannot propagate its exception.
  __device__ std::uint64_t bounded(std::uint64_t exclusive_upper) {
    const std::uint64_t threshold = -exclusive_upper % exclusive_upper;
    for (;;) {
      const std::uint64_t value = next();
      if (value >= threshold) return value % exclusive_upper;
    }
  }

  // Callers must provide ordered endpoints, matching GrammarRandom::integer's
  // successful-call precondition.
  __device__ std::int64_t integer(std::int64_t minimum,
                                  std::int64_t maximum) {
    constexpr std::uint64_t sign = UINT64_C(1) << 63;
    const std::uint64_t first = static_cast<std::uint64_t>(minimum) ^ sign;
    const std::uint64_t last = static_cast<std::uint64_t>(maximum) ^ sign;
    const std::uint64_t width = last - first + 1;
    const std::uint64_t bits =
        (first + (width ? bounded(width) : next())) ^ sign;

    if (bits < sign) return static_cast<std::int64_t>(bits);
    constexpr std::int64_t minimum_int64 =
        -INT64_C(9223372036854775807) - INT64_C(1);
    return minimum_int64 + static_cast<std::int64_t>(bits - sign);
  }

 private:
  std::uint64_t state_;
};

}  // namespace gagp::evo::repro
