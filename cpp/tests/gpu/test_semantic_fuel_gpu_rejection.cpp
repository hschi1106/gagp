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

bool test_fuel_costs_pack() {
  const auto root = gagp::gpu_detail::pack_programs_with_shared_case_count(
      {root_schedule_program()}, 1, 0);
  if (!check(root.all_code.size() == 2 && root.all_code[0].fuel == 3 &&
                 root.all_code[1].fuel == 0,
             "root semantic fuel costs should survive GPU packing")) {
    return false;
  }

  return true;
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

  return true;
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

bool test_bounded_region_preflight_rejection() {
  for (const auto& [program, label] :
       std::vector<std::pair<BytecodeProgram, std::string>>{
           {root_bounded_opcode_program(), "malformed root bounded opcode"},
           {descriptor_only_program(), "descriptor without opcode"}}) {
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

bool test_removed_opcode_values_reject() {
  for (int value = 25; value <= 27; ++value) {
    BytecodeProgram program = constant_program();
    program.code[0].op = static_cast<Opcode>(value);
    const auto packed = gagp::gpu_detail::pack_programs_with_shared_case_count(
        {program}, 1, 0);
    if (!check(packed.metas.size() == 1 && !packed.metas[0].is_valid &&
                   packed.metas[0].err_code == gagp::ErrCode::Type,
               "removed opcode " + std::to_string(value) +
                   " should be rejected by GPU packing")) {
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  gagp::payload::clear();
  if (!test_fuel_costs_pack()) return 1;
  if (!test_malformed_schedules_reject()) return 1;
  if (!test_zero_cost_return_threshold()) return 1;
  if (!test_all_zero_program_at_zero_and_negative_fuel()) return 1;
  if (!test_nonunit_branch_and_check_fuel()) return 1;
  if (!test_bounded_region_preflight_rejection()) return 1;
  if (!test_empty_schedule_packs()) return 1;
  if (!test_removed_opcode_values_reject()) return 1;
  return 0;
}
