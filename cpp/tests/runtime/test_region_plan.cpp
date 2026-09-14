#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/core/region_plan.hpp"

namespace {

using gagp::DomainEndpoint;
using gagp::DuplicatePolicy;
using gagp::RankAxis;
using gagp::RegionBound;
using gagp::RegionBoundKind;
using gagp::RegionCoordinateDomain;
using gagp::RegionPhaseKind;
using gagp::RegionPlan;
using gagp::RegionPreparation;
using gagp::RegionPreparationKind;
using gagp::RegionProgressKind;
using gagp::RegionRequest;
using gagp::RegionSlotBank;
using gagp::RegionStateTransition;
using gagp::RegionTransitionKind;
using gagp::RegionValueSlot;
using gagp::SequenceWindow;
using gagp::ValueTag;
using gagp::WindowEndpoint;
using gagp::WindowEndpointKind;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

template <typename Function>
bool rejects(Function&& function, const std::string& message) {
  try {
    function();
  } catch (const std::invalid_argument&) {
    return true;
  } catch (...) {
    std::cerr << "FAIL: " << message << " (wrong exception type)\n";
    return false;
  }
  std::cerr << "FAIL: " << message << "\n";
  return false;
}

template <typename Mutator>
bool rejects_mutation(const RegionPlan& base, Mutator&& mutate,
                      const std::string& message) {
  RegionPlan changed = base;
  mutate(changed);
  return rejects([&] { gagp::validate_region_plan(changed); }, message);
}

RegionBound literal(std::int64_t value) {
  RegionBound bound;
  bound.literal = value;
  return bound;
}

RegionBound operand(std::uint32_t index) {
  RegionBound bound;
  bound.kind = RegionBoundKind::Operand;
  bound.operand = index;
  return bound;
}

WindowEndpoint endpoint(WindowEndpointKind kind, std::uint32_t cut = 0) {
  return {kind, cut};
}

RegionStateTransition copy_state(std::uint32_t source) {
  RegionStateTransition transition;
  transition.source_state = source;
  return transition;
}

RegionStateTransition coordinate_offset(std::uint32_t source,
                                        std::int64_t offset) {
  RegionStateTransition transition;
  transition.kind = RegionTransitionKind::CoordinateOffset;
  transition.source_state = source;
  transition.offset = offset;
  return transition;
}

RegionStateTransition expression(std::uint32_t index) {
  RegionStateTransition transition;
  transition.kind = RegionTransitionKind::Expression;
  transition.expression = index;
  return transition;
}

RegionStateTransition sequence_window(std::uint32_t source,
                                      WindowEndpoint begin,
                                      WindowEndpoint end) {
  RegionStateTransition transition;
  transition.kind = RegionTransitionKind::SequenceWindow;
  transition.source_state = source;
  transition.window = SequenceWindow{begin, end};
  return transition;
}

std::vector<ValueTag> public_types() {
  return {ValueTag::Int,       ValueTag::Float,     ValueTag::Bool,
          ValueTag::Char,      ValueTag::String,    ValueTag::IntList,
          ValueTag::FloatList, ValueTag::StringList};
}

RegionPlan general_coordinate_plan() {
  RegionPlan plan;
  plan.state_types = {ValueTag::Int, ValueTag::Int, ValueTag::Float};
  plan.result_type = ValueTag::Bool;
  plan.parameter_types = public_types();
  plan.preparations = {{ValueTag::Char, RegionPreparationKind::Identity}};
  plan.request_expression_types = {ValueTag::Float};
  plan.bound_operand_count = 2;
  plan.requests = {
      {{coordinate_offset(0, -2), coordinate_offset(1, 5), expression(0)}},
      {{coordinate_offset(0, 0), coordinate_offset(1, 1), copy_state(2)}},
  };
  plan.coordinate_slots = {1, 0};
  plan.coordinate_rank = {RankAxis{1, 1}, RankAxis{0, -1}};
  plan.coordinate_domains = {
      RegionCoordinateDomain{operand(3), literal(100)},
      RegionCoordinateDomain{literal(-10), operand(4)},
  };
  plan.coordinate_endpoint = DomainEndpoint::Inclusive;
  return plan;
}

RegionPlan sparse_memo_plan() {
  RegionPlan plan;
  plan.state_types = {ValueTag::Int, ValueTag::Int};
  plan.result_type = ValueTag::String;
  plan.requests = {{{coordinate_offset(0, -1), coordinate_offset(1, 0)}}};
  plan.memoized = true;
  plan.limits.cells = 3;
  plan.coordinate_slots = {0, 1};
  plan.coordinate_rank = {{0, 1}, {1, 1}};
  plan.coordinate_domains = {
      {literal(0), literal(100000)},
      {literal(0), literal(100000)},
  };
  return plan;
}

RegionPlan sequence_plan() {
  const WindowEndpoint begin = endpoint(WindowEndpointKind::Begin);
  const WindowEndpoint cut0 = endpoint(WindowEndpointKind::InteriorCut, 0);
  const WindowEndpoint cut1 = endpoint(WindowEndpointKind::InteriorCut, 1);
  const WindowEndpoint end = endpoint(WindowEndpointKind::End);

  RegionPlan plan;
  plan.state_types = {ValueTag::String, ValueTag::Int};
  plan.result_type = ValueTag::Float;
  plan.parameter_types = {ValueTag::Bool};
  plan.preparations = {
      {ValueTag::Int, RegionPreparationKind::InteriorCut},
      {ValueTag::Int, RegionPreparationKind::InteriorCut},
      {ValueTag::Float, RegionPreparationKind::Identity},
  };
  plan.request_expression_types = {ValueTag::Int};
  plan.requests = {
      {{sequence_window(0, begin, cut0), expression(0)}},
      {{sequence_window(0, cut0, cut1), expression(0)}},
      {{sequence_window(0, cut1, end), expression(0)}},
  };
  plan.progress = RegionProgressKind::SequenceWindows;
  plan.sequence_state = 0;
  return plan;
}

RegionPlan typed_auxiliary_plan(ValueTag type) {
  RegionPlan plan;
  plan.state_types = {ValueTag::Int, type};
  plan.result_type = type;
  plan.parameter_types = public_types();
  plan.request_expression_types = {type};
  plan.requests = {{{coordinate_offset(0, -1), expression(0)}}};
  plan.coordinate_slots = {0};
  plan.coordinate_rank = {{0, 1}};
  plan.coordinate_domains = {{literal(0), literal(10)}};
  return plan;
}

bool test_valid_general_plans() {
  gagp::validate_region_plan(general_coordinate_plan());
  gagp::validate_region_plan(sequence_plan());
  gagp::validate_region_plan(sparse_memo_plan());

  RegionPlan zero_cell_memo = sparse_memo_plan();
  zero_cell_memo.limits.cells = 0;
  gagp::validate_region_plan(zero_cell_memo);

  for (ValueTag type : public_types()) {
    gagp::validate_region_plan(typed_auxiliary_plan(type));
  }
  for (ValueTag type : {ValueTag::String, ValueTag::IntList,
                        ValueTag::FloatList, ValueTag::StringList}) {
    RegionPlan sequence = sequence_plan();
    sequence.state_types[0] = type;
    gagp::validate_region_plan(sequence);
  }

  RegionPlan runtime_bounded = general_coordinate_plan();
  runtime_bounded.coordinate_domains[0] = {
      operand(3), literal(std::numeric_limits<std::int64_t>::min())};
  gagp::validate_region_plan(runtime_bounded);
  return true;
}

bool test_coordinate_proof_rejections() {
  const RegionPlan base = general_coordinate_plan();
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.requests[0].states[0] = expression(0);
      }, "an Expression cannot construct a ranked coordinate")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.requests[0].states[0].source_state = 1;
      }, "a ranked coordinate offset must use the same source slot")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.coordinate_slots.clear();
      }, "coordinate progress cannot omit all coordinate slots")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.coordinate_slots = {0, 0};
      }, "coordinate slots must be unique")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.coordinate_slots = {2, 0};
      }, "coordinate slots require exact Int state")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.coordinate_rank.pop_back();
      }, "coordinate rank dimension must match projection")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.coordinate_domains.pop_back();
      }, "coordinate domain dimension must match projection")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.coordinate_rank[0].direction = 0;
      }, "invalid coordinate rank direction is rejected")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.coordinate_rank[0].coordinate = 2;
      }, "rank axes must index the coordinate projection")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.coordinate_endpoint = static_cast<DomainEndpoint>(99);
      }, "invalid coordinate endpoint policy is rejected")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.sequence_state = 1;
      }, "coordinate progress requires canonical unused sequence state")) return false;

  RegionPlan duplicates = typed_auxiliary_plan(ValueTag::Float);
  duplicates.requests.push_back(duplicates.requests.front());
  if (!rejects([&] { gagp::validate_region_plan(duplicates); },
               "duplicate coordinate requests reject by default")) return false;
  duplicates.duplicate_policy = DuplicatePolicy::Allow;
  gagp::validate_region_plan(duplicates);

  RegionPlan literal_reversed = base;
  literal_reversed.coordinate_domains[0] = {literal(5), literal(4)};
  return rejects([&] { gagp::validate_region_plan(literal_reversed); },
                 "a reversed literal domain rejects even beside a dynamic domain");
}

bool test_bounds_and_capacity_rejections() {
  const RegionPlan base = general_coordinate_plan();
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.coordinate_domains[0].lower.kind = static_cast<RegionBoundKind>(99);
      }, "invalid bound kind is rejected")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.coordinate_domains[0].lower.operand = 5;
      }, "bound operand reference must be in range")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.coordinate_domains[0].lower.literal = 1;
      }, "operand bound requires canonical literal zero")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.coordinate_domains[1].lower.operand = 1;
      }, "literal bound requires canonical operand zero")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.coordinate_domains[0].lower = operand(2);
      }, "state bound operand requires exact Int type")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.state_types.clear();
      }, "zero state slots are rejected")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.state_types.assign(gagp::kRecurrenceCoordinateCapacity + 1,
                                ValueTag::Int);
      }, "state slots beyond capacity are rejected")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.requests.clear();
      }, "zero requests are rejected")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        const RegionRequest request = plan.requests.front();
        plan.requests.assign(gagp::kRecurrenceRequestCapacity + 1, request);
      }, "requests beyond capacity are rejected")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.parameter_types.assign(gagp::kRegionParameterCapacity + 1,
                                    ValueTag::Int);
      }, "parameters beyond capacity are rejected")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.preparations.assign(gagp::kRegionPreparationCapacity + 1,
                                 RegionPreparation{ValueTag::Int,
                                                   RegionPreparationKind::Identity});
      }, "preparations beyond capacity are rejected")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.bound_operand_count = gagp::kRegionBoundOperandCapacity + 1;
      }, "bound operands beyond capacity are rejected")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.request_expression_types.assign(
            plan.state_types.size() * plan.requests.size() + 1, ValueTag::Int);
      }, "request expression table beyond state-request capacity is rejected")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.requests[0].states.pop_back();
      }, "every request must construct every state slot")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.limits.entry_fuel = 0;
      }, "zero entry fuel would allow unmetered recursive expansion")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.limits.entry_fuel =
            static_cast<std::uint32_t>(std::numeric_limits<int>::max()) + 1U;
      }, "entry fuel beyond INT_MAX is rejected")) return false;
  return rejects_mutation(base, [](RegionPlan& plan) {
        plan.limits.cells = 1;
      }, "nonmemoized plans require canonical zero cells");
}

bool test_types_enums_and_transition_fields() {
  const RegionPlan base = general_coordinate_plan();
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        ++plan.version;
      }, "unknown region plan version is rejected")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.state_types[2] = ValueTag::FallbackToken;
      }, "internal state type is rejected")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.result_type = ValueTag::Invalid;
      }, "invalid result type is rejected")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.parameter_types[0] = ValueTag::Invalid;
      }, "invalid parameter type is rejected")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.request_expression_types[0] = ValueTag::Invalid;
      }, "invalid request expression type is rejected")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.progress = static_cast<RegionProgressKind>(99);
      }, "invalid progress kind is rejected")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.duplicate_policy = static_cast<DuplicatePolicy>(99);
      }, "invalid duplicate policy is rejected")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.preparations[0].kind = static_cast<RegionPreparationKind>(99);
      }, "invalid preparation kind is rejected")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.preparations[0].type = ValueTag::Invalid;
      }, "invalid preparation type is rejected")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.requests[1].states[2].source_state = 0;
      }, "CopyState requires an exact matching source type")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.requests[1].states[2].offset = 1;
      }, "CopyState inactive fields must be canonical")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.requests[0].states[0].expression = 1;
      }, "CoordinateOffset inactive fields must be canonical")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.requests[0].states[2].expression = 1;
      }, "request expression index must be in range")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.request_expression_types[0] = ValueTag::Int;
      }, "request expression exact type must match its target state")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.requests[0].states[2].source_state = 1;
      }, "Expression inactive source field must be canonical")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.requests[0].states[2].window = {
            endpoint(WindowEndpointKind::End),
            endpoint(WindowEndpointKind::Begin)};
      }, "Expression inactive window must be canonical")) return false;
  return rejects_mutation(base, [](RegionPlan& plan) {
        plan.requests[0].states[2].kind =
            static_cast<RegionTransitionKind>(99);
      }, "invalid transition kind is rejected");
}

bool test_sequence_and_memo_rejections() {
  const RegionPlan base = sequence_plan();
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.preparations[0].type = ValueTag::Float;
      }, "InteriorCut preparation requires exact Int type")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.preparations[0].kind = RegionPreparationKind::Identity;
      }, "a window cut must reference a prepared InteriorCut")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.preparations[0] =
            {ValueTag::Float, RegionPreparationKind::Identity};
      }, "an interior cut cannot reference an unprepared Float slot")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.requests[0].states[0].window.end.cut = 3;
      }, "window cut reference must be prepared")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.requests[0].states[0].window.begin =
            endpoint(WindowEndpointKind::Begin, 1);
      }, "fixed window endpoint fields must be canonical")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.requests[0].states[0].window =
            {endpoint(WindowEndpointKind::Begin), endpoint(WindowEndpointKind::End)};
      }, "ranked sequence request cannot retain the full window")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.requests[0].states[0].source_state = 1;
      }, "ranked window must use the declared sequence state")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.requests[0].states[0].offset = 1;
      }, "SequenceWindow inactive fields must be canonical")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.coordinate_slots = {0};
      }, "sequence progress requires unused coordinate vectors empty")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.coordinate_endpoint = DomainEndpoint::Inclusive;
      }, "sequence progress requires canonical coordinate endpoint")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.bound_operand_count = 1;
      }, "sequence progress requires canonical bound operand count")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.sequence_state = 1;
      }, "ranked sequence state must have a sequence type")) return false;
  if (!rejects_mutation(base, [](RegionPlan& plan) {
        plan.memoized = true;
      }, "sequence progress cannot use coordinate memoization")) return false;

  RegionPlan duplicate_windows = sequence_plan();
  duplicate_windows.requests[1].states[0].window =
      duplicate_windows.requests[0].states[0].window;
  if (!rejects([&] { gagp::validate_region_plan(duplicate_windows); },
               "duplicate sequence windows reject by default")) return false;
  duplicate_windows.duplicate_policy = DuplicatePolicy::Allow;
  gagp::validate_region_plan(duplicate_windows);

  RegionPlan coordinate = general_coordinate_plan();
  coordinate.memoized = true;
  if (!rejects([&] { gagp::validate_region_plan(coordinate); },
               "memoized coordinates cannot omit auxiliary state")) return false;
  coordinate = general_coordinate_plan();
  coordinate.preparations[0].kind = RegionPreparationKind::InteriorCut;
  if (!rejects([&] { gagp::validate_region_plan(coordinate); },
               "coordinate plans cannot use InteriorCut preparation")) return false;

  RegionPlan auxiliary_window = sequence_plan();
  auxiliary_window.state_types[1] = ValueTag::String;
  auxiliary_window.request_expression_types[0] = ValueTag::String;
  auxiliary_window.requests[0].states[1] = sequence_window(
      0, endpoint(WindowEndpointKind::Begin), endpoint(WindowEndpointKind::End));
  return rejects([&] { gagp::validate_region_plan(auxiliary_window); },
                 "auxiliary SequenceWindow must also be proper");
}

bool test_phase_slot_banks() {
  const RegionPlan coordinate = general_coordinate_plan();
  const RegionPlan sequence = sequence_plan();
  const std::vector<RegionPhaseKind> phases{
      RegionPhaseKind::Boundary, RegionPhaseKind::BasePredicate,
      RegionPhaseKind::BaseBody, RegionPhaseKind::Preparation,
      RegionPhaseKind::Request, RegionPhaseKind::Combine};

  for (RegionPhaseKind phase : phases) {
    if (!check(gagp::region_slot_type(coordinate, phase,
                                     {RegionSlotBank::State, 2}) == ValueTag::Float,
               "State bank is visible in every phase")) return false;
    if (!check(gagp::region_slot_type(coordinate, phase,
                                     {RegionSlotBank::Parameter, 0}) == ValueTag::Int,
               "Parameter bank is visible in every phase")) return false;
    if (!check(gagp::region_slot_type(sequence, phase,
                                     {RegionSlotBank::Measure, 0}) == ValueTag::Int,
               "sequence Measure slot zero is visible in every phase")) return false;
  }

  if (!check(gagp::region_slot_type(coordinate, RegionPhaseKind::Combine,
                                   {RegionSlotBank::Result, 1}) == ValueTag::Bool,
             "Result bank exposes result type during Combine")) return false;
  if (!rejects([&] {
        (void)gagp::region_slot_type(coordinate, RegionPhaseKind::Request,
                                    {RegionSlotBank::Result, 0});
      }, "Result bank is hidden outside Combine")) return false;
  if (!rejects([&] {
        (void)gagp::region_slot_type(coordinate, RegionPhaseKind::Combine,
                                    {RegionSlotBank::State, 3});
      }, "State bank checks slot bounds")) return false;
  if (!rejects([&] {
        (void)gagp::region_slot_type(coordinate, RegionPhaseKind::Combine,
                                    {RegionSlotBank::Parameter, 8});
      }, "Parameter bank checks slot bounds")) return false;
  if (!rejects([&] {
        (void)gagp::region_slot_type(sequence, RegionPhaseKind::Request,
                                    {RegionSlotBank::Prepared, 3});
      }, "Prepared bank checks slot bounds")) return false;
  if (!rejects([&] {
        (void)gagp::region_slot_type(coordinate, RegionPhaseKind::Combine,
                                    {RegionSlotBank::Result, 2});
      }, "Result bank checks request bounds")) return false;
  if (!check(gagp::region_slot_type(sequence, RegionPhaseKind::Preparation,
                                   {RegionSlotBank::Prepared, 0}, 1) == ValueTag::Int,
             "Preparation sees an earlier prepared slot")) return false;
  if (!rejects([&] {
        (void)gagp::region_slot_type(sequence, RegionPhaseKind::Preparation,
                                    {RegionSlotBank::Prepared, 1}, 1);
      }, "Preparation cannot see its own prepared slot")) return false;
  if (!rejects([&] {
        (void)gagp::region_slot_type(sequence, RegionPhaseKind::Preparation,
                                    {RegionSlotBank::State, 0},
                                    static_cast<std::uint32_t>(
                                        sequence.preparations.size()));
      }, "Preparation ordinal is checked even for the State bank")) return false;
  if (!check(gagp::region_slot_type(sequence, RegionPhaseKind::Request,
                                   {RegionSlotBank::Prepared, 2}) == ValueTag::Float &&
                 gagp::region_slot_type(sequence, RegionPhaseKind::Combine,
                                        {RegionSlotBank::Prepared, 2}) == ValueTag::Float,
             "Request and Combine see all prepared slots")) return false;
  if (!rejects([&] {
        (void)gagp::region_slot_type(sequence, RegionPhaseKind::BaseBody,
                                    {RegionSlotBank::Prepared, 0});
      }, "base phases cannot see Prepared bank")) return false;
  if (!rejects([&] {
        (void)gagp::region_slot_type(sequence, RegionPhaseKind::Combine,
                                    {RegionSlotBank::Measure, 1});
      }, "Measure exposes only slot zero")) return false;
  if (!rejects([&] {
        (void)gagp::region_slot_type(coordinate, RegionPhaseKind::Combine,
                                    {RegionSlotBank::Measure, 0});
      }, "Measure requires sequence progress")) return false;
  if (!rejects([&] {
        (void)gagp::region_slot_type(coordinate,
                                    static_cast<RegionPhaseKind>(99),
                                    {RegionSlotBank::State, 0});
      }, "unknown phase rejects even for State bank")) return false;
  return rejects([&] {
        (void)gagp::region_slot_type(
            coordinate, RegionPhaseKind::Combine,
            {static_cast<RegionSlotBank>(99), 0});
      }, "unknown slot bank is rejected");
}

}  // namespace

int main() {
  if (!test_valid_general_plans()) return 1;
  if (!test_coordinate_proof_rejections()) return 1;
  if (!test_bounds_and_capacity_rejections()) return 1;
  if (!test_types_enums_and_transition_fields()) return 1;
  if (!test_sequence_and_memo_rejections()) return 1;
  if (!test_phase_slot_banks()) return 1;
  std::cout << "gagp_test_region_plan: OK\n";
  return 0;
}
