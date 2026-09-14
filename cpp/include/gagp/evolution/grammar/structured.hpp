#pragma once

#include <optional>

#include "gagp/evolution/grammar/catalog.hpp"
#include "gagp/core/region_plan.hpp"

namespace gagp::evo::grammar {

enum class StructuredFamily : std::uint8_t { BoundedRecursion, MemoizedRecurrence, BoundedRegion };

// General static-region contracts. No runtime closures or package identities.
struct StructuredContract {
  StructuredFamily family = StructuredFamily::BoundedRecursion;
  std::vector<RType> state_types;
  RType result = RType::Invalid;
  std::uint32_t requests = 0;
  std::string key;
  std::vector<RType> arguments;
  std::vector<RegionSlot> regions;
  std::uint32_t base_predicate = 0;
  std::uint32_t base_body = 0;
  std::uint32_t combine_body = 0;
  std::uint32_t boundary_body = 0;
  std::optional<RegionPlan> plan;
};

inline constexpr std::uint32_t kStructuredStateCapacity = 4;
inline constexpr std::uint32_t kStructuredRequestCapacity = 8;

// Recursive arguments: initial state slots, base predicate/body, ordered request
// state regions (request-major), then combine body. A verified decreasing-rank
// descriptor is additionally required before materialized execution is legal.
StructuredContract recursive_contract(const std::vector<RType>& state, RType result,
    std::uint32_t requests);

// Memoized arguments: initial coordinates, exclusive coordinate extents, boundary
// body, base predicate/body, combine body. Ordered dependency offsets and a verified
// monotone rank are separately serialized execution descriptors. Result tags remain
// exact even though all internal coordinate slots are Int.
StructuredContract memoized_contract(std::uint32_t dimensions, RType result,
    std::uint32_t requests);

StructuredContract bounded_contract(const RegionPlan& plan);

void require_structured_execution(const StructuredContract& contract);

}  // namespace gagp::evo::grammar
