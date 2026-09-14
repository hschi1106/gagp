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
constexpr const char* kBoundedRegionUnsupportedMessage =
    "bounded region execution is not supported by the GPU runtime";

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
                                   const std::string& label,
                                   const char* expected = kUnsupportedMessage) {
  gagp::FitnessSessionGpu session;
  const gagp::FitnessEvalResult result = session.eval_programs({program});
  return check(!result.ok && result.err.code == gagp::ErrCode::Value &&
                   result.err.message == expected,
               label + " should reject unsupported bytecode before the session-ready guard");
}

bool pack_rejects_bounded_region(const BytecodeProgram& program,
                                 const std::string& label) {
  try {
    (void)gagp::gpu_detail::pack_programs_with_shared_case_count(
        {program}, 1, 0);
  } catch (const std::invalid_argument& error) {
    return check(error.what() == std::string(kBoundedRegionUnsupportedMessage),
                 label + " should report the explicit unsupported error");
  } catch (...) {
    return check(false, label + " should throw std::invalid_argument");
  }
  return check(false, label + " should reject bounded regions before packing");
}

BytecodeProgram root_bounded_opcode_program() {
  BytecodeProgram program = constant_program();
  program.code.insert(program.code.begin(), ins_a(Opcode::BoundedRegion, 99));
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

  for (const auto& [program, label] :
       std::vector<std::pair<BytecodeProgram, std::string>>{
           {root_bounded_opcode_program(), "malformed root bounded opcode"},
           {descriptor_only_program(), "descriptor without opcode"},
           {nested_bounded_opcode_program(), "bounded opcode in legacy phase"}}) {
    if (!pack_rejects_bounded_region(program, label + " pack")) return 1;
    if (!uninitialized_session_rejects(
            program, label + " session eval", kBoundedRegionUnsupportedMessage)) {
      return 1;
    }
  }

  if (!test_empty_schedule_packs()) return 1;
  return 0;
}
