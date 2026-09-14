#pragma once

#include <cstdint>
#include <vector>

#include "gagp/evolution/grammar/compiled.hpp"

namespace gagp::evo::grammar {

inline constexpr const char* kGrammarRngVersion = "splitmix64-rejection-v1";

// Explicit algorithm instead of implementation-defined standard distributions.
class GrammarRandom {
 public:
  explicit GrammarRandom(std::uint64_t seed) : state_(seed) {}
  std::uint64_t next();
  std::uint64_t bounded(std::uint64_t exclusive_upper);
  std::int64_t integer(std::int64_t minimum, std::int64_t maximum);
  std::uint32_t production(const CompiledGrammar& grammar,
      const std::vector<std::uint32_t>& eligible);

 private:
  std::uint64_t state_;
};

}  // namespace gagp::evo::grammar
