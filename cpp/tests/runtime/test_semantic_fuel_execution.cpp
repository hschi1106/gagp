#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "gagp/core/bytecode.hpp"
#include "gagp/core/bytecode_verify.hpp"
#include "gagp/core/errors.hpp"
#include "gagp/core/value.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"

namespace {

using namespace gagp;

Instr ins(Opcode op) { return Instr{op, 0, 0, false, false}; }
Instr ins_a(Opcode op, int a) { return Instr{op, a, 0, true, false}; }

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

bool is_int_result(const ExecResult& result, std::int64_t expected) {
  return !result.is_error && result.value.tag == ValueTag::Int &&
         result.value.i == expected;
}

bool is_error(const ExecResult& result, ErrCode expected) {
  return result.is_error && result.err.code == expected;
}

BytecodeProgram weighted_add_program() {
  BytecodeProgram program;
  program.consts = {Value::from_int(2), Value::from_int(3)};
  program.code = {ins_a(Opcode::PushConst, 0),
                  ins_a(Opcode::PushConst, 1), ins(Opcode::Add),
                  ins(Opcode::Return)};
  program.instruction_fuel = {2, 3, 4, 1};
  return program;
}

bool test_weighted_exact_boundary() {
  const BytecodeProgram program = weighted_add_program();
  if (!check(verify_bytecode(program).ok, "weighted program should verify")) {
    return false;
  }
  if (!check(is_int_result(execute_bytecode_cpu(program, {}, 10), 5),
             "exact weighted fuel should execute through RETURN")) {
    return false;
  }
  return check(is_error(execute_bytecode_cpu(program, {}, 9), ErrCode::Timeout),
               "one less than the weighted total should time out");
}

bool test_zero_cost_administration_at_zero_fuel() {
  BytecodeProgram program;
  program.n_locals = 1;
  program.consts = {Value::from_int(17)};
  program.code = {ins_a(Opcode::PushConst, 0), ins_a(Opcode::Store, 0),
                  ins_a(Opcode::Jmp, 3), ins_a(Opcode::Load, 0),
                  ins(Opcode::Return)};
  program.instruction_fuel = {1, 0, 0, 0, 0};
  if (!check(verify_bytecode(program).ok,
             "acyclic zero-cost administration should verify")) {
    return false;
  }
  return check(is_int_result(execute_bytecode_cpu(program, {}, 1), 17),
               "zero-cost administration should run after fuel reaches zero");
}

bool test_charge_precedes_fallible_operation() {
  BytecodeProgram program;
  program.consts = {Value::from_int(1), Value::from_int(0)};
  program.code = {ins_a(Opcode::PushConst, 0),
                  ins_a(Opcode::PushConst, 1), ins(Opcode::Div),
                  ins(Opcode::Return)};
  program.instruction_fuel = {0, 0, 2, 0};
  if (!check(verify_bytecode(program).ok,
             "fallible weighted program should verify")) {
    return false;
  }
  if (!check(is_error(execute_bytecode_cpu(program, {}, 1), ErrCode::Timeout),
             "insufficient charge should time out before division runs")) {
    return false;
  }
  return check(is_error(execute_bytecode_cpu(program, {}, 2), ErrCode::ZeroDiv),
               "an exactly paid fallible instruction should report its runtime error");
}

bool test_empty_schedule_preserves_legacy_fuel() {
  BytecodeProgram program;
  program.consts = {Value::from_int(8)};
  program.code = {ins_a(Opcode::PushConst, 0), ins(Opcode::Return)};
  if (!check(program.instruction_fuel.empty(),
             "default instruction fuel should be empty")) {
    return false;
  }
  if (!check(is_error(execute_bytecode_cpu(program, {}, 1), ErrCode::Timeout),
             "legacy execution should charge one unit for RETURN")) {
    return false;
  }
  return check(is_int_result(execute_bytecode_cpu(program, {}, 2), 8),
               "legacy execution should succeed with one unit per instruction");
}

bool test_verifier_and_raw_runtime_guards() {
  BytecodeProgram malformed;
  malformed.consts = {Value::from_int(1)};
  malformed.code = {ins_a(Opcode::PushConst, 0), ins(Opcode::Return)};
  malformed.instruction_fuel = {1};
  BytecodeVerifyResult verified = verify_bytecode(malformed);
  if (!check(!verified &&
                 verified.diagnostic.code ==
                     BytecodeVerifyCode::InvalidFuelSchedule,
             "verifier should reject a mismatched fuel schedule")) {
    return false;
  }
  if (!check(is_error(execute_bytecode_cpu(malformed, {}, 10), ErrCode::Value),
             "raw runtime should reject a mismatched fuel schedule")) {
    return false;
  }

  BytecodeProgram zero_cycle;
  zero_cycle.code = {ins_a(Opcode::Jmp, 0)};
  zero_cycle.instruction_fuel = {0};
  verified = verify_bytecode(zero_cycle);
  if (!check(!verified &&
                 verified.diagnostic.code ==
                     BytecodeVerifyCode::InvalidFuelSchedule,
             "verifier should reject a zero-cost control-flow cycle")) {
    return false;
  }
  const ExecResult raw = execute_bytecode_cpu(zero_cycle, {}, 0);
  return check(is_error(raw, ErrCode::Value) &&
                   raw.err.message.find("zero-cost") != std::string::npos,
               "raw runtime should stop an unverified zero-cost cycle");
}

BytecodeProgram weighted_phase_program() {
  BytecodeProgram program;
  program.consts = {Value::from_int(0)};
  program.code = {ins_a(Opcode::PushConst, 0),
                  ins_a(Opcode::AsgpDp1d, 0), ins(Opcode::Return)};
  program.instruction_fuel = {1, 0, 0};

  AsgpDp1dSegment segment;
  segment.lo = 0;
  segment.hi = 0;
  segment.base_state = 0;
  segment.boundary_value = Value::from_int(-1);
  segment.dep_kind = -1;
  segment.dep_offsets = {1};
  segment.solve_state_name = 10;
  segment.transition_state_name = 11;
  segment.transition_dep_names = {12};

  segment.solve.consts = {Value::from_int(42)};
  segment.solve.code = {ins_a(Opcode::PushConst, 0), ins(Opcode::Return)};
  segment.solve.instruction_fuel = {3, 0};
  segment.solve.n_locals = 1;
  segment.solve.binder_locals = {{10, 0}};

  segment.transition.code = {ins_a(Opcode::Load, 1), ins(Opcode::Return)};
  segment.transition.n_locals = 2;
  segment.transition.binder_locals = {{11, 0}, {12, 1}};

  program.asgp_dp1d_segments.push_back(std::move(segment));
  return program;
}

bool test_weighted_phase_execution() {
  const BytecodeProgram program = weighted_phase_program();
  if (!check(verify_bytecode(program).ok,
             "program with a weighted phase should verify")) {
    return false;
  }
  if (!check(is_error(execute_bytecode_cpu(program, {}, 4), ErrCode::Timeout),
             "weighted phase should enforce its own instruction charge")) {
    return false;
  }
  return check(is_int_result(execute_bytecode_cpu(program, {}, 5), 42),
               "weighted phase should succeed at its exact aggregate boundary");
}

}  // namespace

int main() {
  if (!test_weighted_exact_boundary()) return 1;
  if (!test_zero_cost_administration_at_zero_fuel()) return 1;
  if (!test_charge_precedes_fallible_operation()) return 1;
  if (!test_empty_schedule_preserves_legacy_fuel()) return 1;
  if (!test_verifier_and_raw_runtime_guards()) return 1;
  if (!test_weighted_phase_execution()) return 1;
  return 0;
}
