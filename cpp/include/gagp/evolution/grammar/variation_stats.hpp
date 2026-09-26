#pragma once

#include <cstdint>

namespace gagp::evo::grammar {

struct VariationCounters {
  // Shared GPU preparation slots, not distinct children or retry attempts.
  std::uint64_t pool_classes = 0;
  std::uint64_t pool_reused_slots = 0;
  std::uint64_t pool_fresh_slots = 0;
  std::uint64_t pool_rejected_slots = 0;

  std::uint64_t crossover_attempts = 0;
  std::uint64_t mutation_attempts = 0;
  std::uint64_t contract_rejections = 0;
  std::uint64_t budget_rejections = 0;
  std::uint64_t generation_rejections = 0;
  std::uint64_t acceptance_rejections = 0;
  std::uint64_t fallback_children = 0;
  std::uint64_t unchanged_children = 0;
  std::uint64_t changed_children = 0;
};

}  // namespace gagp::evo::grammar
