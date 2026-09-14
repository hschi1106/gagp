#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/core/bytecode.hpp"
#include "gagp/core/bytecode_verify.hpp"
#include "gagp/core/errors.hpp"
#include "gagp/evolution/transition/bounded_bytecode.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"

namespace {

using namespace gagp;
using namespace gagp::evo::transition;

Instr ins(Opcode op) { return {op, 0, 0, false, false}; }
Instr ins_a(Opcode op, int a) { return {op, a, 0, true, false}; }

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << '\n';
  return condition;
}

bool same_result(const ExecResult& left, const ExecResult& right) {
  if (left.is_error != right.is_error) return false;
  if (left.is_error) return left.err.code == right.err.code;
  return left.value.tag == right.value.tag && left.value.i == right.value.i;
}

PhaseProgram constant_phase(Value value, int binder, std::uint32_t first_fuel,
                            std::uint32_t second_fuel) {
  PhaseProgram phase;
  phase.consts = {value};
  phase.code = {ins_a(Opcode::PushConst, 0), ins(Opcode::Return)};
  phase.instruction_fuel = {first_fuel, second_fuel};
  phase.n_locals = 1;
  phase.binder_locals[binder] = 0;
  return phase;
}

PhaseProgram dependency_phase(int state_binder, int dependency_binder,
                              std::uint32_t first_fuel,
                              std::uint32_t second_fuel) {
  PhaseProgram phase;
  phase.code = {ins_a(Opcode::Load, 1), ins(Opcode::Return)};
  phase.instruction_fuel = {first_fuel, second_fuel};
  phase.n_locals = 2;
  phase.binder_locals[state_binder] = 0;
  phase.binder_locals[dependency_binder] = 1;
  return phase;
}

AsgpDp1dSegment legacy_segment(std::int64_t result, int binder_base) {
  AsgpDp1dSegment segment;
  segment.lo = 0;
  segment.hi = 2;
  segment.base_state = 0;
  segment.boundary_value = Value::from_int(result);
  segment.dep_kind = -1;
  segment.dep_offsets = {1};
  segment.solve_state_name = binder_base;
  segment.transition_state_name = binder_base + 1;
  segment.transition_dep_names = {binder_base + 2};
  segment.solve = constant_phase(Value::from_int(result), binder_base, 37, 41);
  segment.transition = dependency_phase(binder_base + 1, binder_base + 2,
                                        43, 47);
  return segment;
}

RegionValueSlot slot(RegionSlotBank bank, std::uint32_t index) {
  return {bank, index};
}

RegionPhase region_phase(std::vector<Value> constants,
                         std::vector<Instr> code,
                         std::vector<RegionPhaseBinding> bindings = {},
                         int n_locals = 0) {
  RegionPhase phase;
  phase.program.consts = std::move(constants);
  phase.program.code = std::move(code);
  phase.program.instruction_fuel.assign(phase.program.code.size(), 0);
  phase.program.n_locals = n_locals;
  phase.bindings = std::move(bindings);
  return phase;
}

RegionBound literal(std::int64_t value) {
  RegionBound bound;
  bound.literal = value;
  return bound;
}

BoundedRegionSegment retained_segment() {
  BoundedRegionSegment segment;
  RegionPlan& plan = segment.plan;
  plan.state_types = {ValueTag::Int};
  plan.result_type = ValueTag::Bool;
  RegionStateTransition request;
  request.kind = RegionTransitionKind::CoordinateOffset;
  request.offset = -1;
  plan.requests = {{{request}}};
  plan.limits = {7, 0, 1};
  plan.coordinate_slots = {0};
  plan.coordinate_rank = {{0, 1}};
  plan.coordinate_domains = {{literal(0), literal(2)}};
  plan.coordinate_endpoint = DomainEndpoint::Inclusive;
  segment.boundary = region_phase(
      {Value::from_bool(false)},
      {ins_a(Opcode::PushConst, 0), ins(Opcode::Return)});
  segment.base_predicate = region_phase(
      {Value::from_int(0)},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::PushConst, 0),
       ins(Opcode::Eq), ins(Opcode::Return)},
      {{slot(RegionSlotBank::State, 0), 0}}, 1);
  segment.base_body = region_phase(
      {Value::from_bool(true)},
      {ins_a(Opcode::PushConst, 0), ins(Opcode::Return)});
  segment.combine = region_phase(
      {}, {ins_a(Opcode::Load, 0), ins(Opcode::Return)},
      {{slot(RegionSlotBank::Result, 0), 0}}, 1);
  return segment;
}

BytecodeProgram source_program() {
  BytecodeProgram program;
  program.n_locals = 1;
  program.consts = {Value::from_int(2), Value::from_int(99),
                    Value::from_bool(false)};
  program.code = {
      ins_a(Opcode::PushConst, 2), // 0: fixed false for backward conditional
      ins_a(Opcode::JmpIfTrue, 0), // 1: legal backward target
      ins_a(Opcode::Load, 0),      // 2: branch selector
      ins_a(Opcode::JmpIfFalse, 8),// 3: forward target crosses expansion
      ins_a(Opcode::PushConst, 0), // 4
      ins_a(Opcode::AsgpDp1d, 1), // 5: second legacy table row
      ins_a(Opcode::Jmp, 10),      // 6: forward join
      ins_a(Opcode::Jmp, 11),      // 7: unreachable legal end sentinel
      ins_a(Opcode::PushConst, 0), // 8
      ins_a(Opcode::AsgpDp1d, 0), // 9: first legacy table row
      ins(Opcode::Return),         // 10
  };
  program.instruction_fuel = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31};
  program.asgp_dp1d_segments = {legacy_segment(5, 10),
                                legacy_segment(9, 20)};
  program.bounded_region_segments = {retained_segment()};
  return program;
}

BoundedBytecodeHints hints() {
  BoundedBytecodeHints result;
  result.dp1d = {ValueTag::Int, ValueTag::Int};
  return result;
}

int first_non_timeout(const BytecodeProgram& program,
                      const std::vector<std::pair<int, Value>>& inputs) {
  for (int fuel = 0; fuel <= 1000; ++fuel) {
    const ExecResult result = execute_bytecode_cpu(program, inputs, fuel);
    if (!result.is_error || result.err.code != ErrCode::Timeout) return fuel;
  }
  return -1;
}

bool compare_branch(const BytecodeProgram& source,
                    const BytecodeProgram& lowered, bool branch,
                    std::int64_t expected, const std::string& label) {
  const std::vector<std::pair<int, Value>> inputs{
      {0, Value::from_bool(branch)}};
  const int boundary = first_non_timeout(source, inputs);
  if (!check(boundary >= 0, label + ": no finite fuel boundary")) return false;
  for (int fuel = 0; fuel <= boundary + 3; ++fuel) {
    const ExecResult before = execute_bytecode_cpu(source, inputs, fuel);
    const ExecResult after = execute_bytecode_cpu(lowered, inputs, fuel);
    if (!check(same_result(before, after),
               label + ": result differs at fuel " +
                   std::to_string(fuel))) return false;
  }
  const ExecResult result = execute_bytecode_cpu(lowered, inputs, 1000);
  return check(!result.is_error && result.value.tag == ValueTag::Int &&
                   result.value.i == expected,
               label + ": wrong branch result");
}

bool test_relocation_fuel_and_segment_indices() {
  const BytecodeProgram source = source_program();
  const BytecodeVerifyResult source_verified = verify_bytecode(source);
  if (!check(source_verified.ok,
             "source fixture is invalid: " +
                 source_verified.diagnostic.message)) return false;
  const BytecodeProgram lowered = lower_bounded_bytecode(source, hints());
  const BytecodeVerifyResult lowered_verified = verify_bytecode(lowered);
  if (!check(lowered_verified.ok,
             "lowered fixture is invalid: " +
                 lowered_verified.diagnostic.message)) return false;

  const std::vector<std::uint32_t> expected_fuel{
      2, 3, 5, 7, 11, 13, 0, 17, 19, 23, 29, 0, 31};
  if (!check(lowered.instruction_fuel == expected_fuel,
             "root nonunit fuel was not transferred to expansion anchors") ||
      !check(lowered.code.size() == 13 &&
                 lowered.code[1].op == Opcode::JmpIfTrue &&
                 lowered.code[1].a == 0 &&
                 lowered.code[3].op == Opcode::JmpIfFalse &&
                 lowered.code[3].a == 9 &&
                 lowered.code[7].op == Opcode::Jmp &&
                 lowered.code[7].a == 12 &&
                 lowered.code[8].op == Opcode::Jmp &&
                 lowered.code[8].a == 13,
             "backward, forward, join, or end-sentinel target was not relocated") ||
      !check(lowered.bounded_region_segments.size() == 3 &&
                 lowered.bounded_region_segments[0].plan.result_type ==
                     ValueTag::Bool &&
                 lowered.bounded_region_segments[0].plan.limits.frames == 7 &&
                 lowered.code[6].op == Opcode::BoundedRegion &&
                 lowered.code[6].a == 2 &&
                 lowered.code[11].op == Opcode::BoundedRegion &&
                 lowered.code[11].a == 1,
             "existing or converted segment-table indices changed") ||
      !check(lowered.asgp_dc_segments.empty() &&
                 lowered.asgp_dp1d_segments.empty() &&
                 lowered.asgp_dp2d_segments.empty(),
             "lowered bytecode retained legacy segment tables")) {
    return false;
  }

  const auto& first = lowered.bounded_region_segments[1];
  const auto& second = lowered.bounded_region_segments[2];
  if (!check(first.base_body.program.instruction_fuel ==
                 std::vector<std::uint32_t>({37, 41}) &&
                 first.combine.program.instruction_fuel ==
                 std::vector<std::uint32_t>({43, 47}) &&
                 second.base_body.program.instruction_fuel ==
                 std::vector<std::uint32_t>({37, 41}) &&
                 second.combine.program.instruction_fuel ==
                 std::vector<std::uint32_t>({43, 47}),
             "legacy phase nonunit fuel schedules changed")) {
    return false;
  }
  return compare_branch(source, lowered, false, 5, "false branch") &&
         compare_branch(source, lowered, true, 9, "true branch");
}

bool rejects(const BytecodeProgram& source, const BoundedBytecodeHints& value,
             const std::string& needle, const std::string& label) {
  try {
    (void)lower_bounded_bytecode(source, value);
  } catch (const std::invalid_argument& error) {
    return check(std::string(error.what()).find(needle) != std::string::npos,
                 label + ": wrong diagnostic: " + error.what());
  }
  return check(false, label + ": transition accepted invalid hints");
}

bool test_hint_count_and_tag_rejection() {
  const BytecodeProgram source = source_program();
  BoundedBytecodeHints wrong_count;
  wrong_count.dp1d = {ValueTag::Int};
  if (!rejects(source, wrong_count, "counts", "hint count")) return false;
  BoundedBytecodeHints wrong_tag = hints();
  wrong_tag.dp1d[1] = ValueTag::Invalid;
  if (!rejects(source, wrong_tag, "exact public type", "invalid result tag"))
    return false;
  wrong_tag = hints();
  wrong_tag.dp1d[0] = ValueTag::FallbackToken;
  return rejects(source, wrong_tag, "exact public type",
                 "fallback result tag");
}

bool test_outside_base_preserves_structure_checks() {
  BytecodeProgram source = source_program();
  source.asgp_dp1d_segments[0].base_state = 3;
  if (!check(!verify_bytecode(source).ok,
             "outside-base source should retain its legacy verifier rejection"))
    return false;
  const BytecodeProgram lowered = lower_bounded_bytecode(source, hints());
  if (!compare_branch(source, lowered, false, 5, "outside-base boundary") ||
      !compare_branch(source, lowered, true, 9, "other segment unchanged"))
    return false;

  // Normalizing only the base in a verification copy must not conceal a
  // malformed instruction, even in the unreachable solve phase.
  source.asgp_dp1d_segments[0].solve.code[0].a = 99;
  return rejects(source, hints(), "source structure", "outside-base bad constant");
}

}  // namespace

int main() {
  try {
    if (!test_relocation_fuel_and_segment_indices() ||
        !test_hint_count_and_tag_rejection() ||
        !test_outside_base_preserves_structure_checks()) return 1;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
