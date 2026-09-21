#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/core/bytecode.hpp"
#include "gagp/core/errors.hpp"
#include "gagp/core/value.hpp"
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "gagp/runtime/gpu/host_pack_gpu.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace {

using gagp::BytecodeProgram;
using gagp::Instr;
using gagp::Opcode;
using gagp::Value;

constexpr double kPenalty = 7.0;

Instr ins(Opcode op) { return Instr{op, 0, 0, false, false}; }
Instr ins_a(Opcode op, int a) { return Instr{op, a, 0, true, false}; }

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

BytecodeProgram constant_program() {
  BytecodeProgram program;
  program.consts = {Value::from_int(7)};
  program.code = {ins_a(Opcode::PushConst, 0), ins(Opcode::Return)};
  return program;
}

BytecodeProgram root_schedule_program() {
  BytecodeProgram program = constant_program();
  program.instruction_fuel = {3, 0};
  return program;
}

BytecodeProgram weighted_phase_program() {
  BytecodeProgram program;
  program.consts = {Value::from_int(0)};
  program.code = {ins_a(Opcode::PushConst, 0),
                  ins_a(Opcode::AsgpDp1d, 0), ins(Opcode::Return)};
  program.instruction_fuel = {1, 0, 0};

  gagp::AsgpDp1dSegment segment;
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

bool test_fuel_costs_pack() {
  const auto root = gagp::gpu_detail::pack_programs_with_shared_case_count(
      {root_schedule_program()}, 1, 0);
  if (!check(root.all_code.size() == 2 && root.all_code[0].fuel == 3 &&
                 root.all_code[1].fuel == 0,
             "root semantic fuel costs should survive GPU packing")) {
    return false;
  }

  const auto phase = gagp::gpu_detail::pack_programs_with_shared_case_count(
      {weighted_phase_program()}, 1, 0);
  return check(phase.all_code.size() == 3 &&
                   phase.all_code[0].fuel == 1 &&
                   phase.all_code[1].fuel == 0 &&
                   phase.all_code[2].fuel == 0 &&
                   phase.all_phase_code.size() == 4 &&
                   phase.all_phase_code[0].fuel == 3 &&
                   phase.all_phase_code[1].fuel == 0 &&
                   phase.all_phase_code[2].fuel == 1 &&
                   phase.all_phase_code[3].fuel == 1,
               "root and legacy phase fuel costs should pack exactly");
}

bool pack_rejects_schedule(const BytecodeProgram& program,
                           const std::string& expected_fragment,
                           const std::string& label) {
  try {
    (void)gagp::gpu_detail::pack_programs_with_shared_case_count(
        {program}, 1, 0);
  } catch (const std::invalid_argument& error) {
    return check(std::string(error.what()).find(expected_fragment) !=
                     std::string::npos,
                 label + " should report its malformed semantic fuel schedule");
  } catch (...) {
    return check(false, label + " should throw std::invalid_argument");
  }
  return check(false, label + " should reject before packing");
}

bool session_rejects_schedule(gagp::FitnessSessionGpu* session,
                              const BytecodeProgram& program,
                              const std::string& expected_fragment,
                              const std::string& label) {
  const auto result = session->eval_programs({program});
  return check(!result.ok && result.err.code == gagp::ErrCode::Value &&
                   result.err.message.find(expected_fragment) !=
                       std::string::npos,
               label + " should reject through the initialized GPU session");
}

bool test_malformed_schedules_reject() {
  gagp::FitnessSessionGpu session;
  const std::vector<gagp::CaseBindings> cases(1);
  const auto init = session.init(cases, {Value::from_int(0)}, 100, 1,
                                 kPenalty);
  if (!init.ok) {
    return check(false, "malformed schedule GPU initialization failed: " +
                            init.err.message);
  }

  BytecodeProgram root_length = constant_program();
  root_length.instruction_fuel = {1};
  if (!pack_rejects_schedule(root_length, "root semantic fuel schedule",
                             "root schedule length mismatch") ||
      !session_rejects_schedule(&session, root_length,
                                "root semantic fuel schedule",
                                "root schedule length mismatch")) {
    return false;
  }

  BytecodeProgram root_cycle;
  root_cycle.code = {ins_a(Opcode::Jmp, 0)};
  root_cycle.instruction_fuel = {0};
  if (!pack_rejects_schedule(root_cycle, "zero-cost control-flow cycle",
                             "root zero-cost cycle") ||
      !session_rejects_schedule(&session, root_cycle,
                                "zero-cost control-flow cycle",
                                "root zero-cost cycle")) {
    return false;
  }

  BytecodeProgram phase_length = weighted_phase_program();
  phase_length.asgp_dp1d_segments[0].solve.instruction_fuel = {3};
  if (!pack_rejects_schedule(phase_length, "phase semantic fuel schedule",
                             "phase schedule length mismatch") ||
      !session_rejects_schedule(&session, phase_length,
                                "phase semantic fuel schedule",
                                "phase schedule length mismatch")) {
    return false;
  }

  BytecodeProgram phase_cycle = weighted_phase_program();
  auto& solve = phase_cycle.asgp_dp1d_segments[0].solve;
  solve.consts.clear();
  solve.code = {ins_a(Opcode::Jmp, 0)};
  solve.instruction_fuel = {0};
  return pack_rejects_schedule(phase_cycle, "zero-cost control-flow cycle",
                               "phase zero-cost cycle") &&
         session_rejects_schedule(&session, phase_cycle,
                                  "zero-cost control-flow cycle",
                                  "phase zero-cost cycle");
}

bool eval_fitness_at_fuel(const BytecodeProgram& program, const Value& expected,
                          int fuel, double expected_fitness,
                          const std::string& label) {
  gagp::FitnessSessionGpu session;
  const std::vector<gagp::CaseBindings> cases(1);
  const auto init = session.init(cases, {expected}, fuel, 1, kPenalty);
  if (!init.ok) {
    return check(false, label + " GPU initialization failed: " +
                            init.err.message);
  }
  const auto result = session.eval_programs({program});
  if (!result.ok) {
    return check(false,
                 label + " GPU evaluation failed: " + result.err.message);
  }
  return check(result.fitness.size() == 1 &&
                   std::fabs(result.fitness[0] - expected_fitness) <= 1e-12,
               label + " fitness mismatch");
}

bool test_zero_cost_return_threshold() {
  BytecodeProgram program = constant_program();
  program.instruction_fuel = {1, 0};
  return eval_fitness_at_fuel(program, Value::from_int(7), 0, -kPenalty,
                              "zero-cost RETURN below threshold") &&
         eval_fitness_at_fuel(program, Value::from_int(7), 1, 0.0,
                              "zero-cost RETURN exact threshold");
}

bool test_all_zero_program_at_zero_and_negative_fuel() {
  BytecodeProgram program = constant_program();
  program.instruction_fuel = {0, 0};
  return eval_fitness_at_fuel(program, Value::from_int(7), -1, -kPenalty,
                              "all-zero program with negative fuel") &&
         eval_fitness_at_fuel(program, Value::from_int(7), 0, 0.0,
                              "all-zero program with zero fuel");
}

BytecodeProgram weighted_branch_check_int_program() {
  BytecodeProgram program;
  program.consts = {Value::from_bool(true), Value::from_int(7),
                    Value::from_int(99)};
  program.code = {
      ins_a(Opcode::PushConst, 0), ins_a(Opcode::JmpIfFalse, 5),
      ins_a(Opcode::PushConst, 1), ins(Opcode::CheckInt),
      ins_a(Opcode::Jmp, 7),       ins_a(Opcode::PushConst, 2),
      ins(Opcode::CheckInt),       ins(Opcode::Return),
  };
  program.instruction_fuel = {2, 0, 3, 0, 0, 11, 0, 0};
  return program;
}

bool test_nonunit_branch_and_check_fuel() {
  const BytecodeProgram int_program = weighted_branch_check_int_program();
  if (!eval_fitness_at_fuel(int_program, Value::from_int(7), 4, -kPenalty,
                            "weighted branch CHECK_INT below threshold") ||
      !eval_fitness_at_fuel(int_program, Value::from_int(7), 5, 0.0,
                            "weighted branch CHECK_INT exact threshold")) {
    return false;
  }

  BytecodeProgram list_program;
  const Value list = gagp::payload::make_int_list_value(
      {Value::from_int(2), Value::from_int(3)});
  list_program.consts = {list};
  list_program.code = {ins_a(Opcode::PushConst, 0), ins(Opcode::CheckList),
                       ins(Opcode::Return)};
  list_program.instruction_fuel = {4, 0, 0};
  return eval_fitness_at_fuel(list_program, list, 3, -kPenalty,
                              "weighted CHECK_LIST below threshold") &&
         eval_fitness_at_fuel(list_program, list, 4, 1.0,
                              "weighted CHECK_LIST exact threshold");
}

bool test_weighted_legacy_phase_threshold() {
  const BytecodeProgram program = weighted_phase_program();
  return eval_fitness_at_fuel(program, Value::from_int(42), 4, -kPenalty,
                              "weighted DP phase below threshold") &&
         eval_fitness_at_fuel(program, Value::from_int(42), 5, 0.0,
                              "weighted DP phase exact threshold");
}

bool uninitialized_session_rejects(const BytecodeProgram& program,
                                   const std::string& label) {
  gagp::FitnessSessionGpu session;
  const auto result = session.eval_programs({program});
  return check(!result.ok && result.err.code == gagp::ErrCode::Value &&
                   result.err.message == "gpu fitness session is not initialized",
               label + " should reject unsupported bytecode before the session-ready guard");
}

bool pack_rejects_bounded_region(const BytecodeProgram& program,
                                 const std::string& label) {
  try {
    (void)gagp::gpu_detail::pack_programs_with_shared_case_count(
        {program}, 1, 0);
  } catch (const std::invalid_argument& error) {
    return check(std::string(error.what()).find("bounded region bytecode verification failed:") == 0,
                 label + " should report the explicit unsupported error");
  } catch (...) {
    return check(false, label + " should throw std::invalid_argument");
  }
  return check(false, label + " should reject bounded regions before packing");
}

BytecodeProgram root_bounded_opcode_program() {
  BytecodeProgram program = constant_program();
  program.code.insert(program.code.begin(),
                      ins_a(Opcode::BoundedRegion, 99));
  return program;
}

BytecodeProgram descriptor_only_program() {
  BytecodeProgram program = constant_program();
  program.bounded_region_segments.emplace_back();
  return program;
}

BytecodeProgram nested_bounded_opcode_program() {
  BytecodeProgram program = constant_program();
  gagp::AsgpDcSegment segment;
  segment.solve.code = {ins_a(Opcode::BoundedRegion, 0)};
  program.asgp_dc_segments.push_back(std::move(segment));
  return program;
}

bool test_bounded_region_preflight_rejection() {
  for (const auto& [program, label] :
       std::vector<std::pair<BytecodeProgram, std::string>>{
           {root_bounded_opcode_program(), "malformed root bounded opcode"},
           {descriptor_only_program(), "descriptor without opcode"},
           {nested_bounded_opcode_program(), "bounded opcode in legacy phase"}}) {
    if (!pack_rejects_bounded_region(program, label + " pack") ||
        !uninitialized_session_rejects(program, label + " session eval")) {
      return false;
    }
  }
  return true;
}

bool test_empty_schedule_packs() {
  const auto packed = gagp::gpu_detail::pack_programs_with_shared_case_count(
      {constant_program()}, 1, 0);
  return check(packed.metas.size() == 1 && packed.all_code.size() == 2 &&
                   packed.all_code[0].fuel == 1 &&
                   packed.all_code[1].fuel == 1 && packed.total_cases == 1,
               "an empty fuel schedule should retain legacy unit charging");
}

}  // namespace

int main() {
  gagp::payload::clear();
  if (!test_fuel_costs_pack()) return 1;
  if (!test_malformed_schedules_reject()) return 1;
  if (!test_zero_cost_return_threshold()) return 1;
  if (!test_all_zero_program_at_zero_and_negative_fuel()) return 1;
  if (!test_nonunit_branch_and_check_fuel()) return 1;
  if (!test_weighted_legacy_phase_threshold()) return 1;
  if (!test_bounded_region_preflight_rejection()) return 1;
  if (!test_empty_schedule_packs()) return 1;
  return 0;
}
