#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "gagp/core/bytecode.hpp"
#include "gagp/core/errors.hpp"
#include "gagp/runtime/cpu/fitness_cpu.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/cpu/execution_session.hpp"

namespace {

using namespace gagp;

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

RegionValueSlot slot(RegionSlotBank bank, std::uint32_t index) {
  return {bank, index};
}

RegionPhase phase(std::vector<Value> constants, std::vector<Instr> code,
                  std::vector<RegionPhaseBinding> bindings = {},
                  int n_locals = 0) {
  RegionPhase result;
  result.program.consts = std::move(constants);
  result.program.code = std::move(code);
  result.program.n_locals = n_locals;
  result.program.instruction_fuel.assign(result.program.code.size(), 0);
  result.bindings = std::move(bindings);
  return result;
}

RegionPhase constant_phase(std::int64_t value) {
  return phase({Value::from_int(value)},
               {ins_a(Opcode::PushConst, 0), ins(Opcode::Return)});
}

RegionStateTransition decrement() {
  RegionStateTransition result;
  result.kind = RegionTransitionKind::CoordinateOffset;
  result.source_state = 0;
  result.offset = -1;
  return result;
}

RegionBound literal(std::int64_t value) {
  RegionBound result;
  result.literal = value;
  return result;
}

BoundedRegionSegment counter_segment() {
  BoundedRegionSegment segment;
  RegionPlan& plan = segment.plan;
  plan.state_types = {ValueTag::Int};
  plan.result_type = ValueTag::Int;
  plan.parameter_types = {ValueTag::Int};
  plan.requests = {{{decrement()}}};
  plan.limits = {16, 16, 1};
  plan.memoized = true;
  plan.coordinate_slots = {0};
  plan.coordinate_rank = {{0, 1}};
  plan.coordinate_domains = {{literal(0), literal(5)}};
  plan.coordinate_endpoint = DomainEndpoint::Inclusive;

  segment.parameter_locals = {1};
  segment.boundary = constant_phase(-1);
  segment.base_predicate = phase(
      {Value::from_int(0)},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::PushConst, 0),
       ins(Opcode::Eq), ins(Opcode::Return)},
      {{slot(RegionSlotBank::State, 0), 0}}, 1);
  segment.base_body = phase(
      {}, {ins_a(Opcode::Load, 0), ins(Opcode::Return)},
      {{slot(RegionSlotBank::Parameter, 0), 0}}, 1);
  segment.combine = phase(
      {Value::from_int(1)},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::PushConst, 0),
       ins(Opcode::Add), ins(Opcode::Return)},
      {{slot(RegionSlotBank::Result, 0), 0}}, 1);
  return segment;
}

BytecodeProgram counter_program() {
  BytecodeProgram program;
  program.n_locals = 2;
  program.code = {ins_a(Opcode::Load, 0),
                  ins_a(Opcode::BoundedRegion, 0), ins(Opcode::Return)};
  program.bounded_region_segments = {counter_segment()};
  return program;
}

BytecodeProgram two_segment_program() {
  BoundedRegionSegment captured = counter_segment();
  BoundedRegionSegment fixed = counter_segment();
  fixed.plan.parameter_types.clear();
  fixed.parameter_locals.clear();
  fixed.base_body = constant_phase(9);

  BytecodeProgram program;
  program.n_locals = 3;
  program.code = {
      ins_a(Opcode::Load, 0), ins_a(Opcode::BoundedRegion, 0),
      ins_a(Opcode::Load, 2), ins_a(Opcode::BoundedRegion, 1),
      ins(Opcode::Add), ins(Opcode::Return),
  };
  program.bounded_region_segments = {std::move(captured), std::move(fixed)};
  return program;
}

bool compare_range(CpuExecutionSession* session,
                   const BytecodeProgram& program,
                   const std::vector<std::pair<int, Value>>& inputs,
                   int maximum, const std::string& label) {
  for (int fuel = 0; fuel <= maximum; ++fuel) {
    const ExecResult expected = execute_bytecode_cpu(program, inputs, fuel);
    const ExecResult actual = session->execute(inputs, fuel);
    if (!check(same_result(expected, actual),
               label + " differs at fuel " + std::to_string(fuel))) {
      return false;
    }
  }
  return true;
}

bool test_reuse_inputs_memo_and_capture_errors() {
  const BytecodeProgram program = counter_program();
  CpuExecutionSession session(program);
  const std::vector<std::vector<std::pair<int, Value>>> cases{
      {{0, Value::from_int(3)}, {1, Value::from_int(10)}},
      {{0, Value::from_int(1)}, {1, Value::from_int(40)}},
      {{0, Value::from_int(3)}, {1, Value::from_int(20)}},
      {{0, Value::from_int(2)}},
      {{0, Value::from_int(2)}, {1, Value::from_bool(true)}},
      {{0, Value::from_int(6)}},
  };
  for (std::size_t i = 0; i < cases.size(); ++i) {
    if (!compare_range(&session, program, cases[i], 14,
                       "reused session case " + std::to_string(i))) {
      return false;
    }
  }

  const ExecResult first = session.execute(cases[0], 100);
  const ExecResult changed_capture = session.execute(cases[2], 100);
  const ExecResult missing = session.execute(cases[3], 100);
  const ExecResult wrong = session.execute(cases[4], 100);
  const ExecResult unused_missing = session.execute(cases[5], 100);
  return check(!first.is_error && first.value.i == 13,
               "memo counter returned the wrong first captured value") &&
         check(!changed_capture.is_error && changed_capture.value.i == 23,
               "memo contents leaked across executions with a new capture") &&
         check(missing.is_error && missing.err.code == ErrCode::Name,
               "unset used capture did not raise Name") &&
         check(wrong.is_error && wrong.err.code == ErrCode::Type,
               "wrong-tag used capture did not raise Type") &&
         check(!unused_missing.is_error && unused_missing.value.i == -1,
               "unused unset capture affected the boundary path") &&
         check(!session.execute(cases[1], 100).is_error,
               "session did not recover after capture errors");
}

bool test_immutable_snapshot_and_independent_sessions() {
  BytecodeProgram caller = counter_program();
  const BytecodeProgram original = caller;
  CpuExecutionSession snapshot(caller);
  caller.code.clear();
  caller.bounded_region_segments[0].plan.requests.clear();
  const std::vector<std::pair<int, Value>> inputs{
      {0, Value::from_int(2)}, {1, Value::from_int(7)}};
  const ExecResult expected = execute_bytecode_cpu(original, inputs, 100);
  const ExecResult actual = snapshot.execute(inputs, 100);
  if (!check(same_result(expected, actual) && !actual.is_error &&
                 actual.value.i == 9,
             "session did not retain an immutable program snapshot")) {
    return false;
  }

  CpuExecutionSession left(counter_program());
  CpuExecutionSession right(counter_program());
  const ExecResult left_first = left.execute(
      {{0, Value::from_int(4)}, {1, Value::from_int(1)}}, 100);
  const ExecResult right_first = right.execute(
      {{0, Value::from_int(4)}, {1, Value::from_int(100)}}, 100);
  const ExecResult left_again = left.execute(
      {{0, Value::from_int(4)}, {1, Value::from_int(3)}}, 100);
  return check(!left_first.is_error && left_first.value.i == 5 &&
                   !right_first.is_error && right_first.value.i == 104 &&
                   !left_again.is_error && left_again.value.i == 7,
               "execution sessions shared memo or capture state");
}

bool test_deferred_invalid_segment_validation() {
  BytecodeProgram unreachable = counter_program();
  unreachable.bounded_region_segments[0].plan.requests.clear();
  unreachable.consts = {Value::from_int(42)};
  unreachable.code = {ins_a(Opcode::PushConst, 0), ins(Opcode::Return)};
  unreachable.n_locals = 0;
  CpuExecutionSession unused(unreachable);
  const ExecResult unused_expected = execute_bytecode_cpu(unreachable, {}, 2);
  const ExecResult unused_actual = unused.execute({}, 2);
  if (!check(same_result(unused_expected, unused_actual) &&
                 !unused_actual.is_error && unused_actual.value.i == 42,
             "constructor eagerly rejected an unreachable invalid segment")) {
    return false;
  }

  BytecodeProgram reached = counter_program();
  reached.bounded_region_segments[0].plan.requests.clear();
  CpuExecutionSession deferred(reached);
  const std::vector<std::pair<int, Value>> inputs{{0, Value::from_int(2)}};
  return compare_range(&deferred, reached, inputs, 4,
                       "deferred invalid segment validation");
}

bool test_move_lifetime_and_retained_storage() {
  static_assert(!std::is_copy_constructible_v<CpuExecutionSession>);
  static_assert(!std::is_copy_assignable_v<CpuExecutionSession>);
  static_assert(std::is_nothrow_move_constructible_v<CpuExecutionSession>);
  static_assert(std::is_nothrow_move_assignable_v<CpuExecutionSession>);

  CpuExecutionSession source(counter_program());
  const auto inputs = std::vector<std::pair<int, Value>>{
      {0, Value::from_int(5)}, {1, Value::from_int(8)}};
  CpuExecutionSession moved(std::move(source));
  const ExecResult moved_from_source = source.execute(inputs, 100);
  const ExecResult after_move = moved.execute(inputs, 100);
  if (!check(moved_from_source.is_error &&
                 moved_from_source.err.code == ErrCode::Value,
             "moved-from session did not return a stable Value error") ||
      !check(!after_move.is_error && after_move.value.i == 13,
             "move construction lost the program or execution state")) {
    return false;
  }
  CpuExecutionSession assigned(counter_program());
  assigned = std::move(moved);
  const ExecResult moved_from_again = moved.execute(inputs, 100);
  const ExecResult after_assignment = assigned.execute(inputs, 100);
  if (!check(moved_from_again.is_error &&
                 moved_from_again.err.code == ErrCode::Value,
             "move-assigned source did not return a stable Value error") ||
      !check(!after_assignment.is_error && after_assignment.value.i == 13,
             "move assignment lost the program or execution state")) {
    return false;
  }

  const std::size_t retained = assigned.retained_region_bytes();
  if (!check(retained > 0,
             "memoized session retained no reusable region storage")) {
    return false;
  }
  for (int i = 0; i < 64; ++i) {
    const ExecResult result = assigned.execute(inputs, 100);
    if (!check(!result.is_error && result.value.i == 13,
               "repeated retained-storage execution changed result") ||
        !check(assigned.retained_region_bytes() == retained,
               "repeated execution grew retained frame or memo storage")) {
      return false;
    }
  }
  return true;
}

bool test_fitness_batch_reuses_without_state_leaks() {
  const BytecodeProgram program = counter_program();
  const std::vector<CaseBindings> cases{
      CaseBindings{{0, Value::from_int(3)}, {1, Value::from_int(10)}},
      CaseBindings{{0, Value::from_int(2)}},
      CaseBindings{{0, Value::from_int(3)}, {1, Value::from_int(20)}},
  };
  const std::vector<Value> answers{
      Value::from_int(13), Value::from_int(0), Value::from_int(20)};
  constexpr double penalty = 7.0;

  double one_shot = 0.0;
  for (std::size_t i = 0; i < cases.size(); ++i) {
    std::vector<std::pair<int, Value>> inputs;
    for (const InputBinding& binding : cases[i])
      inputs.push_back({binding.idx, binding.value});
    const ExecResult result = execute_bytecode_cpu(program, inputs, 100);
    if (result.is_error) {
      one_shot -= penalty;
    } else {
      const double difference = static_cast<double>(result.value.i) -
                                static_cast<double>(answers[i].i);
      one_shot -= difference < 0.0 ? -difference : difference;
    }
  }
  const std::vector<double> fitness = eval_fitness_cpu(
      {program}, cases, answers, 100, penalty, 1);
  return check(one_shot == -10.0,
               "one-shot batch oracle changed unexpectedly") &&
         check(fitness.size() == 1 && fitness[0] == one_shot,
               "fitness session leaked memo, errors, or captures across cases");
}

bool test_two_segments_share_no_invocation_state() {
  const BytecodeProgram program = two_segment_program();
  CpuExecutionSession session(program);
  const std::vector<std::pair<int, Value>> inputs{
      {0, Value::from_int(2)}, {1, Value::from_int(5)},
      {2, Value::from_int(3)}};
  if (!compare_range(&session, program, inputs, 20,
                     "two bounded segments in one root")) {
    return false;
  }
  const ExecResult result = session.execute(inputs, 100);
  if (!check(!result.is_error && result.value.tag == ValueTag::Int &&
                 result.value.i == 19,
             "two bounded segments reused each other's scratch or memo")) {
    return false;
  }
  const std::vector<std::pair<int, Value>> changed{
      {0, Value::from_int(1)}, {1, Value::from_int(20)},
      {2, Value::from_int(2)}};
  const ExecResult changed_result = session.execute(changed, 100);
  const ExecResult changed_expected =
      execute_bytecode_cpu(program, changed, 100);
  return check(same_result(changed_result, changed_expected) &&
                   !changed_result.is_error && changed_result.value.i == 32,
               "two-segment session retained invocation state across runs");
}

}  // namespace

int main() {
  try {
    if (!test_reuse_inputs_memo_and_capture_errors() ||
        !test_immutable_snapshot_and_independent_sessions() ||
        !test_deferred_invalid_segment_validation() ||
        !test_move_lifetime_and_retained_storage() ||
        !test_fitness_batch_reuses_without_state_leaks() ||
        !test_two_segments_share_no_invocation_state()) {
      return 1;
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
