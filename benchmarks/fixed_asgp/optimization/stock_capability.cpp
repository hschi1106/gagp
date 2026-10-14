// Standalone CPU preflight for the exact local ASGP StockWithCooldown DP shape.
// Source contract: ASGP src/task/dp_problems.cc StockWithCooldown declares
// (t+1,h), (t+1,h+1), (t+2,h-1); src/eval/dp_scheme.cc Runner::Evaluate
// clamps both coordinates to the instance extent before each recursive call.
// GAGP boundary phases terminate a child; they cannot reenter its clamped state.
// For example (t=0,h=0) requests (2,0) in ASGP, not boundary state (2,-1).
// Changing to nonmemoized recursion changes benchmark work and is not a fix.
//
// From repository root, compile the actual contract implementation:
// c++ -std=c++20 -O0 -Icpp/include \
//   benchmarks/fixed_asgp/optimization/stock_capability.cpp \
//   cpp/src/runtime/region_plan.cpp cpp/src/runtime/recurrence_rank.cpp \
//   cpp/src/runtime/sequence_rank.cpp -o /tmp/gagp-stock-capability
// /tmp/gagp-stock-capability
// This performs no CUDA work and changes neither runtime nor frozen inputs.
#include "gagp/core/region_plan.hpp"
#include <iostream>
#include <stdexcept>
using namespace gagp;

RegionStateTransition offset(unsigned slot, int value) {
  RegionStateTransition out;
  out.kind = RegionTransitionKind::CoordinateOffset;
  out.source_state = slot;
  out.offset = value;
  return out;
}
RegionPlan stock_offsets() {
  RegionPlan p;
  p.state_types = {ValueTag::Int, ValueTag::Int};
  p.result_type = ValueTag::Int;
  p.memoized = true;
  p.limits = {32, 26, 1};
  p.coordinate_slots = {0, 1};
  p.coordinate_rank = {{0, -1}, {1, 1}};
  p.coordinate_domains = {{{}, {RegionBoundKind::Literal, 12, 0}},
                          {{}, {RegionBoundKind::Literal, 1, 0}}};
  p.coordinate_endpoint = DomainEndpoint::Inclusive;
  p.requests = {{{offset(0, 1), offset(1, 0)}},
                {{offset(0, 1), offset(1, 1)}},
                {{offset(0, 2), offset(1, -1)}}};
  return p;
}
bool rejected(const char* name, const RegionPlan& p, const char* expected) {
  try { validate_region_plan(p); }
  catch (const std::invalid_argument& e) {
    std::cout << name << ": " << e.what() << '\n';
    return std::string(e.what()) == expected;
  }
  std::cerr << name << ": unexpectedly accepted\n";
  return false;
}
int main() {
  auto p = stock_offsets();
  validate_region_plan(p);
  std::cout << "raw_offsets_positive_control: accepted\n";
  // An expression could implement min(max(h + delta, 0), 1), but it is
  // prohibited on any coordinate used in the progress/memo descriptor.
  p.request_expression_types = {ValueTag::Int};
  p.requests[1].states[1] = {};
  p.requests[1].states[1].kind = RegionTransitionKind::Expression;
  if (!rejected("clamped_memo_coordinate", p,
      "each projected coordinate must use its matching offset constructor")) return 1;
  // Removing h from the rank permits its clamped expression, but then the
  // memo key cannot contain h and the memoized plan is rejected.
  p.coordinate_slots = {0};
  p.coordinate_rank = {{0, -1}};
  p.coordinate_domains.resize(1);
  p.duplicate_policy = DuplicatePolicy::Allow;
  if (!rejected("clamped_auxiliary_memo_state", p,
      "memoized region requires coordinates covering every state slot")) return 1;
  p.memoized = false;
  p.limits.cells = 0;
  validate_region_plan(p);
  std::cout << "nonmemoized_control: accepted (excluded from fair DP benchmark)\n";
  std::cout << "stock_with_cooldown: unsupported_exact_clamped_memo_transitions\n";
}
