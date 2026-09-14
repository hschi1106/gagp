#pragma once

#include <cstdint>
#include <vector>

#include "gagp/core/recurrence_rank.hpp"
#include "gagp/core/sequence_rank.hpp"
#include "gagp/core/value.hpp"

namespace gagp {

inline constexpr std::uint32_t kRegionPlanVersion = 1;
inline constexpr std::uint32_t kRegionParameterCapacity = 32;
inline constexpr std::uint32_t kRegionPreparationCapacity = 4;
inline constexpr std::uint32_t kRegionBoundOperandCapacity = 8;

enum class RegionProgressKind : std::uint8_t { Coordinates, SequenceWindows };
enum class RegionBoundKind : std::uint8_t { Literal, Operand };

// Operand indices refer to initial states followed by additional Int operands.
// Bounds are invocation values, never reevaluated while visiting recursive states.
struct RegionBound {
  RegionBoundKind kind = RegionBoundKind::Literal;
  std::int64_t literal = 0;
  std::uint32_t operand = 0;
};

struct RegionCoordinateDomain {
  RegionBound lower;
  RegionBound upper;
};

enum class RegionPreparationKind : std::uint8_t { Identity, InteriorCut };

struct RegionPreparation {
  ValueTag type = ValueTag::Invalid;
  RegionPreparationKind kind = RegionPreparationKind::Identity;
};

enum class RegionTransitionKind : std::uint8_t {
  CopyState,
  CoordinateOffset,
  SequenceWindow,
  Expression,
};

// Rank-affecting slots must use a checked constructor, never Expression. Other
// slots may use an exact typed request phase. Inactive fields are canonical zeros.
struct RegionStateTransition {
  RegionTransitionKind kind = RegionTransitionKind::CopyState;
  std::uint32_t source_state = 0;
  std::int64_t offset = 0;
  SequenceWindow window{};
  std::uint32_t expression = 0;
};

struct RegionRequest {
  std::vector<RegionStateTransition> states;
};

struct RegionExecutionLimits {
  std::uint32_t frames = 64;
  std::uint32_t cells = 0;
  // Positive in materialized plans: every visited state is metered even when
  // its phase code has zero-cost instructions.
  std::uint32_t entry_fuel = 1;
};

// Shared typed shape for native lowering and bytecode materialization. Phase code
// and physical local/binder mappings belong to their respective representations.
struct RegionPlan {
  std::uint32_t version = kRegionPlanVersion;
  std::vector<ValueTag> state_types;
  ValueTag result_type = ValueTag::Invalid;
  std::vector<ValueTag> parameter_types;
  std::vector<RegionPreparation> preparations;
  std::vector<ValueTag> request_expression_types;
  std::uint32_t bound_operand_count = 0;
  std::vector<RegionRequest> requests;
  RegionExecutionLimits limits;
  bool memoized = false;
  DuplicatePolicy duplicate_policy = DuplicatePolicy::Reject;
  RegionProgressKind progress = RegionProgressKind::Coordinates;

  // Coordinate proof: axes index coordinate_slots, which project typed state.
  // Domains may use literal or invocation-operand bounds, with explicit endpoints.
  std::vector<std::uint32_t> coordinate_slots;
  std::vector<RankAxis> coordinate_rank;
  std::vector<RegionCoordinateDomain> coordinate_domains;
  DomainEndpoint coordinate_endpoint = DomainEndpoint::Exclusive;

  // Sequence proof: mandatory length<=1 base selection precedes preparations.
  // The ranked state in each request uses a proper window of this same source.
  std::uint32_t sequence_state = 0;
};

enum class RegionSlotBank : std::uint8_t { State, Parameter, Prepared, Result, Measure };

struct RegionValueSlot {
  RegionSlotBank bank = RegionSlotBank::State;
  std::uint32_t slot = 0;
};

enum class RegionPhaseKind : std::uint8_t {
  Boundary,
  BasePredicate,
  BaseBody,
  Preparation,
  Request,
  Combine,
};

// Static shape/progress validation. Runtime operand bounds are additionally
// resolved and checked before invoking a materialized region.
void validate_region_plan(const RegionPlan& plan);

// Returns the exact source type, rejecting banks invisible in the selected phase.
// For a preparation phase, only earlier preparation slots are visible.
ValueTag region_slot_type(const RegionPlan& plan, RegionPhaseKind phase,
                          const RegionValueSlot& slot, std::uint32_t preparation = 0);

// Shared flat expression/phase layout, independent of native AST ownership.
inline constexpr std::size_t kBoundedRegionArityCapacity = 52;
inline constexpr std::size_t kBoundedRegionPhaseBindingCapacity = 49;
std::size_t bounded_region_arity(const RegionPlan& plan);
RegionPhaseKind bounded_region_phase_kind(const RegionPlan& plan, std::size_t ordinal);
ValueTag bounded_region_phase_type(const RegionPlan& plan, std::size_t ordinal);
std::uint32_t bounded_region_preparation_ordinal(const RegionPlan& plan, std::size_t ordinal);

}  // namespace gagp
