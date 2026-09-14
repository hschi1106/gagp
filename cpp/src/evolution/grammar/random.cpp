#include "gagp/evolution/grammar/random.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace gagp::evo::grammar {

std::uint64_t GrammarRandom::next() {
  auto value = (state_ += UINT64_C(0x9e3779b97f4a7c15));
  value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31);
}

std::uint64_t GrammarRandom::bounded(std::uint64_t exclusive_upper) {
  if (!exclusive_upper) throw std::invalid_argument("random bound must be positive");
  const auto threshold = -exclusive_upper % exclusive_upper;
  for (;;) {
    const auto value = next();
    if (value >= threshold) return value % exclusive_upper;
  }
}

std::int64_t GrammarRandom::integer(std::int64_t minimum, std::int64_t maximum) {
  if (minimum > maximum) throw std::invalid_argument("random integer endpoints reversed");
  constexpr auto sign = UINT64_C(1) << 63;
  const auto first = static_cast<std::uint64_t>(minimum) ^ sign;
  const auto last = static_cast<std::uint64_t>(maximum) ^ sign;
  const auto width = last - first + 1;
  const auto bits = (first + (width ? bounded(width) : next())) ^ sign;
  std::int64_t result;
  static_assert(sizeof(result) == sizeof(bits), "64-bit integer sampling requires exact width");
  std::memcpy(&result, &bits, sizeof(result));
  return result;
}

std::uint32_t GrammarRandom::production(const CompiledGrammar& grammar,
    const std::vector<std::uint32_t>& eligible) {
  if (eligible.empty()) throw std::invalid_argument("no grammar production fits the requested context and budget");
  double maximum = 0;
  for (auto id : eligible) maximum = std::max(maximum, grammar.productions().at(id).weight);
  double total = 0;
  for (auto id : eligible) total += grammar.productions()[id].weight / maximum;
  const auto unit = static_cast<double>(next() >> 11) * 0x1.0p-53;
  double remaining = unit * total;
  for (auto id : eligible) {
    remaining -= grammar.productions()[id].weight / maximum;
    if (remaining < 0) return id;
  }
  // Only rounding at the upper edge can reach here; never enable an excluded rule.
  return eligible.back();
}

}  // namespace gagp::evo::grammar
