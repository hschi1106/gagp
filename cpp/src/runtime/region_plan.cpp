#include "gagp/core/region_plan.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace gagp {
namespace {

bool public_type(ValueTag type) {
  switch (type) {
    case ValueTag::Int:
    case ValueTag::Float:
    case ValueTag::Bool:
    case ValueTag::Char:
    case ValueTag::String:
    case ValueTag::IntList:
    case ValueTag::FloatList:
    case ValueTag::StringList:
      return true;
    case ValueTag::FallbackToken:
    case ValueTag::Invalid:
      return false;
  }
  return false;
}

bool sequence_type(ValueTag type) {
  return type == ValueTag::String || type == ValueTag::IntList ||
         type == ValueTag::FloatList || type == ValueTag::StringList;
}

bool default_endpoint(const WindowEndpoint& endpoint) {
  return endpoint.kind == WindowEndpointKind::Begin && endpoint.cut == 0;
}

bool default_window(const SequenceWindow& window) {
  return default_endpoint(window.begin) && default_endpoint(window.end);
}

void validate_public_type(ValueTag type, const char* context) {
  if (!public_type(type)) {
    throw std::invalid_argument(std::string(context) +
                                " requires an exact public value type");
  }
}

void validate_bound(const RegionPlan& plan, const RegionBound& bound) {
  switch (bound.kind) {
    case RegionBoundKind::Literal:
      if (bound.operand != 0) {
        throw std::invalid_argument(
            "literal region bound requires canonical operand zero");
      }
      return;
    case RegionBoundKind::Operand: {
      if (bound.literal != 0) {
        throw std::invalid_argument(
            "operand region bound requires canonical literal zero");
      }
      const std::size_t operand_count =
          plan.state_types.size() + plan.bound_operand_count;
      if (bound.operand >= operand_count) {
        throw std::invalid_argument("region bound operand is out of range");
      }
      if (bound.operand < plan.state_types.size() &&
          plan.state_types[bound.operand] != ValueTag::Int) {
        throw std::invalid_argument(
            "region bound state operand must have exact Int type");
      }
      return;
    }
  }
  throw std::invalid_argument("invalid region bound kind");
}

void validate_window_endpoint(const RegionPlan& plan,
                              const WindowEndpoint& endpoint) {
  switch (endpoint.kind) {
    case WindowEndpointKind::Begin:
    case WindowEndpointKind::End:
      if (endpoint.cut != 0) {
        throw std::invalid_argument(
            "fixed sequence endpoint requires canonical cut zero");
      }
      return;
    case WindowEndpointKind::InteriorCut:
      if (endpoint.cut >= plan.preparations.size()) {
        throw std::invalid_argument(
            "sequence endpoint preparation index is out of range");
      }
      if (plan.preparations[endpoint.cut].kind !=
              RegionPreparationKind::InteriorCut ||
          plan.preparations[endpoint.cut].type != ValueTag::Int) {
        throw std::invalid_argument(
            "sequence interior cut must reference an Int InteriorCut preparation");
      }
      return;
  }
  throw std::invalid_argument("invalid sequence window endpoint kind");
}

void validate_window(const RegionPlan& plan, const SequenceWindow& window) {
  validate_window_endpoint(plan, window.begin);
  validate_window_endpoint(plan, window.end);
}

void validate_transition(const RegionPlan& plan,
                         const RegionStateTransition& transition,
                         std::size_t target_state) {
  const auto state_count = plan.state_types.size();
  const ValueTag target_type = plan.state_types[target_state];
  switch (transition.kind) {
    case RegionTransitionKind::CopyState:
      if (transition.source_state >= state_count) {
        throw std::invalid_argument("copied region state is out of range");
      }
      if (plan.state_types[transition.source_state] != target_type) {
        throw std::invalid_argument(
            "copied region state requires an exact matching type");
      }
      if (transition.offset != 0 || transition.expression != 0 ||
          !default_window(transition.window)) {
        throw std::invalid_argument(
            "copied region state has noncanonical inactive fields");
      }
      return;
    case RegionTransitionKind::CoordinateOffset:
      if (transition.source_state >= state_count) {
        throw std::invalid_argument("offset region state is out of range");
      }
      if (plan.state_types[transition.source_state] != ValueTag::Int ||
          target_type != ValueTag::Int) {
        throw std::invalid_argument(
            "coordinate offset requires exact Int source and target states");
      }
      if (transition.expression != 0 || !default_window(transition.window)) {
        throw std::invalid_argument(
            "coordinate offset has noncanonical inactive fields");
      }
      return;
    case RegionTransitionKind::SequenceWindow:
      if (transition.source_state >= state_count) {
        throw std::invalid_argument("windowed region state is out of range");
      }
      if (plan.progress != RegionProgressKind::SequenceWindows ||
          transition.source_state != plan.sequence_state ||
          !sequence_type(target_type) ||
          plan.state_types[transition.source_state] != target_type) {
        throw std::invalid_argument(
            "sequence window requires the ranked sequence source and an exact matching target");
      }
      if (transition.offset != 0 || transition.expression != 0) {
        throw std::invalid_argument(
            "sequence window has noncanonical inactive fields");
      }
      validate_window(plan, transition.window);
      validate_sequence_progress(SequenceProgress{
          static_cast<std::uint32_t>(plan.preparations.size()),
          {transition.window}, plan.duplicate_policy});
      return;
    case RegionTransitionKind::Expression:
      if (transition.expression >= plan.request_expression_types.size()) {
        throw std::invalid_argument(
            "region request expression index is out of range");
      }
      if (plan.request_expression_types[transition.expression] != target_type) {
        throw std::invalid_argument(
            "region request expression has the wrong exact state type");
      }
      if (transition.source_state != 0 || transition.offset != 0 ||
          !default_window(transition.window)) {
        throw std::invalid_argument(
            "region request expression has noncanonical inactive fields");
      }
      return;
  }
  throw std::invalid_argument("invalid region state transition kind");
}

void validate_preparations(const RegionPlan& plan) {
  for (const RegionPreparation& preparation : plan.preparations) {
    validate_public_type(preparation.type, "region preparation");
    switch (preparation.kind) {
      case RegionPreparationKind::Identity:
        break;
      case RegionPreparationKind::InteriorCut:
        if (plan.progress != RegionProgressKind::SequenceWindows ||
            preparation.type != ValueTag::Int) {
          throw std::invalid_argument(
              "InteriorCut preparation requires sequence progress and exact Int type");
        }
        break;
      default:
        throw std::invalid_argument("invalid region preparation kind");
    }
  }
}

void validate_coordinate_plan(const RegionPlan& plan) {
  if (plan.sequence_state != 0) {
    throw std::invalid_argument(
        "coordinate progress requires canonical sequence state zero");
  }
  if (plan.coordinate_slots.empty() ||
      plan.coordinate_slots.size() > plan.state_types.size()) {
    throw std::invalid_argument(
        "coordinate progress requires between one and all state slots");
  }
  if (plan.coordinate_rank.size() != plan.coordinate_slots.size() ||
      plan.coordinate_domains.size() != plan.coordinate_slots.size()) {
    throw std::invalid_argument(
        "coordinate slots, rank, and domains must have equal dimensions");
  }

  std::set<std::uint32_t> unique_slots;
  for (std::uint32_t slot : plan.coordinate_slots) {
    if (slot >= plan.state_types.size() ||
        plan.state_types[slot] != ValueTag::Int ||
        !unique_slots.insert(slot).second) {
      throw std::invalid_argument(
          "coordinate slots must be unique in-range exact Int states");
    }
  }

  if (plan.coordinate_endpoint != DomainEndpoint::Exclusive &&
      plan.coordinate_endpoint != DomainEndpoint::Inclusive) {
    throw std::invalid_argument("invalid coordinate domain endpoint policy");
  }

  bool all_literal = true;
  std::vector<CoordinateDomain> literal_domains;
  literal_domains.reserve(plan.coordinate_domains.size());
  for (const RegionCoordinateDomain& domain : plan.coordinate_domains) {
    validate_bound(plan, domain.lower);
    validate_bound(plan, domain.upper);
    if (domain.lower.kind == RegionBoundKind::Literal &&
        domain.upper.kind == RegionBoundKind::Literal &&
        domain.lower.literal > domain.upper.literal) {
      throw std::invalid_argument(
          "literal coordinate domain lower bound exceeds upper bound");
    }
    all_literal &= domain.lower.kind == RegionBoundKind::Literal &&
                   domain.upper.kind == RegionBoundKind::Literal;
    literal_domains.push_back({domain.lower.literal, domain.upper.literal});
  }

  std::vector<std::vector<std::int64_t>> offsets;
  offsets.reserve(plan.requests.size());
  for (const RegionRequest& request : plan.requests) {
    std::vector<std::int64_t> request_offsets;
    request_offsets.reserve(plan.coordinate_slots.size());
    for (std::uint32_t slot : plan.coordinate_slots) {
      const RegionStateTransition& transition = request.states[slot];
      if (transition.kind != RegionTransitionKind::CoordinateOffset ||
          transition.source_state != slot) {
        throw std::invalid_argument(
            "each projected coordinate must use its matching offset constructor");
      }
      request_offsets.push_back(transition.offset);
    }
    offsets.push_back(std::move(request_offsets));
  }

  validate_coordinate_progress(plan.coordinate_slots.size(), plan.coordinate_rank,
                               offsets, plan.duplicate_policy);
  if (all_literal) {
    validate_coordinate_recurrence(CoordinateRecurrence{
        std::move(literal_domains), plan.coordinate_endpoint,
        plan.coordinate_rank, std::move(offsets), plan.duplicate_policy});
  }
}

void validate_sequence_plan(const RegionPlan& plan) {
  if (!plan.coordinate_slots.empty() || !plan.coordinate_rank.empty() ||
      !plan.coordinate_domains.empty()) {
    throw std::invalid_argument(
        "sequence progress requires canonical empty coordinate vectors");
  }
  if (plan.coordinate_endpoint != DomainEndpoint::Exclusive) {
    throw std::invalid_argument(
        "sequence progress requires canonical Exclusive coordinate endpoint");
  }
  if (plan.bound_operand_count != 0) {
    throw std::invalid_argument(
        "sequence progress requires canonical zero bound operands");
  }
  if (plan.sequence_state >= plan.state_types.size() ||
      !sequence_type(plan.state_types[plan.sequence_state])) {
    throw std::invalid_argument(
        "sequence progress requires an in-range ranked sequence state");
  }

  std::vector<SequenceWindow> windows;
  windows.reserve(plan.requests.size());
  for (const RegionRequest& request : plan.requests) {
    const RegionStateTransition& transition = request.states[plan.sequence_state];
    if (transition.kind != RegionTransitionKind::SequenceWindow ||
        transition.source_state != plan.sequence_state) {
      throw std::invalid_argument(
          "each ranked sequence state must use a window of the same source state");
    }
    windows.push_back(transition.window);
  }
  validate_sequence_progress(SequenceProgress{
      static_cast<std::uint32_t>(plan.preparations.size()),
      std::move(windows), plan.duplicate_policy});
}

void validate_phase(RegionPhaseKind phase) {
  switch (phase) {
    case RegionPhaseKind::Boundary:
    case RegionPhaseKind::BasePredicate:
    case RegionPhaseKind::BaseBody:
    case RegionPhaseKind::Preparation:
    case RegionPhaseKind::Request:
    case RegionPhaseKind::Combine:
      return;
  }
  throw std::invalid_argument("invalid region phase kind");
}

}  // namespace

void validate_region_plan(const RegionPlan& plan) {
  if (plan.version != kRegionPlanVersion) {
    throw std::invalid_argument("unsupported region plan version");
  }
  if (plan.state_types.empty() ||
      plan.state_types.size() > kRecurrenceCoordinateCapacity) {
    throw std::invalid_argument("region plan requires 1..4 state slots");
  }
  if (plan.parameter_types.size() > kRegionParameterCapacity) {
    throw std::invalid_argument("region parameter count exceeds capacity");
  }
  if (plan.preparations.size() > kRegionPreparationCapacity) {
    throw std::invalid_argument("region preparation count exceeds capacity");
  }
  if (plan.bound_operand_count > kRegionBoundOperandCapacity) {
    throw std::invalid_argument("region bound operand count exceeds capacity");
  }
  if (plan.requests.empty() ||
      plan.requests.size() > kRecurrenceRequestCapacity) {
    throw std::invalid_argument("region plan requires 1..8 ordered requests");
  }
  if (plan.request_expression_types.size() >
      plan.state_types.size() * plan.requests.size()) {
    throw std::invalid_argument(
        "region request expression type table exceeds request-state capacity");
  }
  if (plan.limits.entry_fuel == 0 || plan.limits.entry_fuel >
      static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    throw std::invalid_argument("region entry fuel must be between 1 and INT_MAX");
  }
  if (!plan.memoized && plan.limits.cells != 0) {
    throw std::invalid_argument(
        "nonmemoized region requires canonical zero cell limit");
  }

  for (ValueTag type : plan.state_types) {
    validate_public_type(type, "region state");
  }
  validate_public_type(plan.result_type, "region result");
  for (ValueTag type : plan.parameter_types) {
    validate_public_type(type, "region parameter");
  }
  for (ValueTag type : plan.request_expression_types) {
    validate_public_type(type, "region request expression");
  }
  validate_preparations(plan);

  for (const RegionRequest& request : plan.requests) {
    if (request.states.size() != plan.state_types.size()) {
      throw std::invalid_argument(
          "each region request must construct every state slot");
    }
    for (std::size_t state = 0; state < request.states.size(); ++state) {
      validate_transition(plan, request.states[state], state);
    }
  }

  switch (plan.progress) {
    case RegionProgressKind::Coordinates:
      validate_coordinate_plan(plan);
      break;
    case RegionProgressKind::SequenceWindows:
      validate_sequence_plan(plan);
      break;
    default:
      throw std::invalid_argument("invalid region progress kind");
  }

  if (plan.memoized) {
    if (plan.progress != RegionProgressKind::Coordinates ||
        plan.coordinate_slots.size() != plan.state_types.size()) {
      throw std::invalid_argument(
          "memoized region requires coordinates covering every state slot");
    }
  }
}

ValueTag region_slot_type(const RegionPlan& plan, RegionPhaseKind phase,
                          const RegionValueSlot& slot,
                          std::uint32_t preparation) {
  validate_phase(phase);
  if (phase == RegionPhaseKind::Preparation &&
      preparation >= plan.preparations.size()) {
    throw std::invalid_argument("region preparation ordinal is out of range");
  }
  switch (slot.bank) {
    case RegionSlotBank::State:
      if (slot.slot >= plan.state_types.size()) {
        throw std::invalid_argument("region state slot is out of range");
      }
      validate_public_type(plan.state_types[slot.slot], "region state slot");
      return plan.state_types[slot.slot];
    case RegionSlotBank::Parameter:
      if (slot.slot >= plan.parameter_types.size()) {
        throw std::invalid_argument("region parameter slot is out of range");
      }
      validate_public_type(plan.parameter_types[slot.slot],
                           "region parameter slot");
      return plan.parameter_types[slot.slot];
    case RegionSlotBank::Prepared:
      if (slot.slot >= plan.preparations.size()) {
        throw std::invalid_argument("region prepared slot is out of range");
      }
      if (phase != RegionPhaseKind::Request &&
          phase != RegionPhaseKind::Combine &&
          !(phase == RegionPhaseKind::Preparation && slot.slot < preparation)) {
        throw std::invalid_argument(
            "prepared slot is not visible in this region phase");
      }
      validate_public_type(plan.preparations[slot.slot].type,
                           "region prepared slot");
      return plan.preparations[slot.slot].type;
    case RegionSlotBank::Result:
      if (phase != RegionPhaseKind::Combine) {
        throw std::invalid_argument(
            "request result slot is visible only in the combine phase");
      }
      if (slot.slot >= plan.requests.size()) {
        throw std::invalid_argument("region request result slot is out of range");
      }
      validate_public_type(plan.result_type, "region request result slot");
      return plan.result_type;
    case RegionSlotBank::Measure:
      if (plan.progress != RegionProgressKind::SequenceWindows || slot.slot != 0) {
        throw std::invalid_argument(
            "region measure slot requires sequence progress and slot zero");
      }
      return ValueTag::Int;
  }
  throw std::invalid_argument("invalid region slot bank");
}

}  // namespace gagp

namespace gagp {
namespace {

std::size_t phase_count(const RegionPlan& plan) {
  return 3 + plan.preparations.size() +
         plan.request_expression_types.size() +
         (plan.progress == RegionProgressKind::Coordinates ? 1 : 0);
}

void require_phase(const RegionPlan& plan, std::size_t ordinal) {
  if (ordinal >= phase_count(plan)) {
    throw std::invalid_argument("bounded region phase ordinal is out of range");
  }
}

}  // namespace

std::size_t bounded_region_arity(const RegionPlan& plan) {
  const std::size_t arity = plan.state_types.size() +
                            plan.bound_operand_count + phase_count(plan);
  if (arity > kBoundedRegionArityCapacity) {
    throw std::invalid_argument("bounded region prefix arity exceeds capacity");
  }
  return arity;
}

RegionPhaseKind bounded_region_phase_kind(const RegionPlan& plan,
                                           std::size_t ordinal) {
  require_phase(plan, ordinal);
  if (ordinal == 0) return RegionPhaseKind::BasePredicate;
  if (ordinal == 1) return RegionPhaseKind::BaseBody;
  ordinal -= 2;
  if (ordinal < plan.preparations.size()) {
    return RegionPhaseKind::Preparation;
  }
  ordinal -= plan.preparations.size();
  if (ordinal < plan.request_expression_types.size()) {
    return RegionPhaseKind::Request;
  }
  ordinal -= plan.request_expression_types.size();
  if (ordinal == 0) return RegionPhaseKind::Combine;
  return RegionPhaseKind::Boundary;
}

ValueTag bounded_region_phase_type(const RegionPlan& plan,
                                   std::size_t ordinal) {
  const RegionPhaseKind kind = bounded_region_phase_kind(plan, ordinal);
  switch (kind) {
    case RegionPhaseKind::BasePredicate:
      return ValueTag::Bool;
    case RegionPhaseKind::Preparation:
      return plan.preparations[bounded_region_preparation_ordinal(plan,
                                                                  ordinal)]
          .type;
    case RegionPhaseKind::Request: {
      const std::size_t expression =
          ordinal - 2 - plan.preparations.size();
      return plan.request_expression_types[expression];
    }
    case RegionPhaseKind::Boundary:
    case RegionPhaseKind::BaseBody:
    case RegionPhaseKind::Combine:
      return plan.result_type;
  }
  throw std::invalid_argument("invalid bounded region phase kind");
}

std::uint32_t bounded_region_preparation_ordinal(const RegionPlan& plan,
                                                 std::size_t ordinal) {
  require_phase(plan, ordinal);
  if (ordinal < 2 || ordinal - 2 >= plan.preparations.size()) return 0;
  return static_cast<std::uint32_t>(ordinal - 2);
}


}  // namespace gagp
