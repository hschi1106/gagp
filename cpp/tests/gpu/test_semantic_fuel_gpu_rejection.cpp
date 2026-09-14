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

namespace {

using gagp::BytecodeProgram;
using gagp::Instr;
using gagp::Opcode;
using gagp::Value;

constexpr const char* kUnsupportedMessage =
    "semantic fuel schedules are not supported by the GPU runtime";

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
  program.instruction_fuel = {1, 0};
  return program;
}

BytecodeProgram nested_schedule_program() {
  BytecodeProgram program = constant_program();
  gagp::AsgpDp1dSegment segment;
  segment.solve.consts = {Value::from_int(11)};
  segment.solve.code = {ins_a(Opcode::PushConst, 0), ins(Opcode::Return)};
  segment.solve.instruction_fuel = {1, 0};
  program.asgp_dp1d_segments.push_back(std::move(segment));
  return program;
}

bool pack_rejects(const BytecodeProgram& program, const std::string& label) {
  try {
    (void)gagp::gpu_detail::pack_programs_with_shared_case_count(
        {program}, 1, 0);
  } catch (const std::invalid_argument& error) {
    return check(error.what() == std::string(kUnsupportedMessage),
                 label + " should report the explicit unsupported error");
  } catch (...) {
    return check(false, label + " should throw std::invalid_argument");
  }
  return check(false, label + " should reject semantic fuel before packing");
}

bool uninitialized_session_rejects(const BytecodeProgram& program,
                                   const std::string& label) {
  gagp::FitnessSessionGpu session;
  const gagp::FitnessEvalResult result = session.eval_programs({program});
  return check(!result.ok && result.err.code == gagp::ErrCode::Value &&
                   result.err.message == kUnsupportedMessage,
               label + " should reject semantic fuel before the session-ready guard");
}

bool test_empty_schedule_packs() {
  const gagp::gpu_detail::PackResult packed =
      gagp::gpu_detail::pack_programs_with_shared_case_count(
          {constant_program()}, 1, 0);
  return check(packed.metas.size() == 1 && packed.all_code.size() == 2 &&
                   packed.total_cases == 1,
               "an empty fuel schedule should preserve legacy GPU packing");
}

}  // namespace

int main() {
  const BytecodeProgram root = root_schedule_program();
  if (!pack_rejects(root, "root schedule pack")) return 1;
  if (!uninitialized_session_rejects(root, "root schedule session eval")) {
    return 1;
  }

  const BytecodeProgram nested = nested_schedule_program();
  if (!pack_rejects(nested, "nested phase schedule pack")) return 1;
  if (!uninitialized_session_rejects(nested,
                                     "nested phase schedule session eval")) {
    return 1;
  }

  if (!test_empty_schedule_packs()) return 1;
  return 0;
}
