#pragma once
#include "../view_profile.hpp"

namespace gagp::gpu_detail {
using detail::view_value_type;
using detail::view_code_supported;

// Capability proof after ordinary bytecode verification: one direct region
// invocation, with no root arithmetic/control/local writes or extra stack values.
inline bool direct_region_root_supported(const BytecodeProgram& program) {
  if (program.bounded_region_segments.size() != 1 || program.code.size() < 3 ||
      program.code.back().op != Opcode::Return) return false;
  const auto& region = program.code[program.code.size() - 2];
  if (region.op != Opcode::BoundedRegion || !region.has_a || region.a != 0) return false;
  const auto& plan = program.bounded_region_segments[0].plan;
  const auto count = plan.state_types.size() + plan.bound_operand_count;
  if (count > 12) return false;
  std::size_t stack = 0;
  for (std::size_t i = 0; i + 2 < program.code.size(); ++i) {
    const auto& ins = program.code[i];
    if (ins.op == Opcode::Load || ins.op == Opcode::PushConst) {
      if (++stack > 12) return false;
    } else if (ins.op == Opcode::CallBuiltin && ins.has_a && ins.has_b &&
        ins.a == static_cast<int>(BuiltinId::Len) && ins.b == 1 && stack > 0) {
      // Existing type-flow proves the operand is an IntList.
    } else if ((ins.op == Opcode::CheckInt || ins.op == Opcode::CheckList) && stack > 0) {
    } else return false;
  }
  if (stack != count) return false;
  return true;
}

inline bool view_program_supported(const BytecodeProgram& program,
    const std::array<ValueTag, 64>& input_types) {
  for (const auto& segment : program.bounded_region_segments)
    if (!detail::view_region_supported(segment)) return false;
  return view_code_supported(program.code, program.consts, program.n_locals, input_types, ValueTag::Invalid, &program.bounded_region_segments);
}
}  // namespace gagp::gpu_detail
