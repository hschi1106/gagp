#pragma once

#include <array>
#include <vector>
#include "gagp/core/builtin.hpp"
#include "gagp/core/bytecode.hpp"

namespace gagp::gpu_detail {

// Conservative, forward-only abstract interpretation. This proves containers
// can only be observed through Len/Index/Slice and never escape as final values.
// It is capability detection AFTER ordinary validation, not a replacement for it.
inline bool view_value_type(ValueTag type) {
  return type == ValueTag::Int || type == ValueTag::Bool || type == ValueTag::IntList;
}
inline bool view_code_supported(const std::vector<Instr>& code,
    const std::vector<Value>& constants, int n_locals,
    const std::array<ValueTag, 64>& input_types, ValueTag expected,
    const std::vector<BoundedRegionSegment>* regions = nullptr) {
  if (n_locals < 0 || n_locals > 64 || code.empty()) return false;
  for (const auto& value : constants) if (!view_value_type(value.tag)) return false;
  struct State {
    bool reached = false;
    int sp = 0;
    std::array<ValueTag, 64> stack{}, locals{};
  };
  std::vector<State> states(code.size() + 1);
  states[0].reached = true; states[0].locals = input_types;
  auto merge = [&](std::size_t target, const State& state) {
    if (target > code.size()) return false;
    auto& old = states[target];
    if (!old.reached) { old = state; return true; }
    if (old.sp != state.sp || old.locals != state.locals) return false;
    for (int i = 0; i < old.sp; ++i) if (old.stack[i] != state.stack[i]) return false;
    return true;
  };
  auto result_type = [&](ValueTag type) {
    return expected == ValueTag::Invalid ? (type == ValueTag::Int || type == ValueTag::Bool) : type == expected;
  };
  bool returns = false;
  for (std::size_t ip = 0; ip < code.size(); ++ip) {
    if (!states[ip].reached) continue;
    auto state = states[ip];
    const auto& ins = code[ip];
    auto pop = [&](ValueTag type) {
      return state.sp > 0 && state.stack[--state.sp] == type;
    };
    auto push = [&](ValueTag type) {
      if (state.sp >= 64) return false;
      state.stack[state.sp++] = type; return true;
    };
    switch (ins.op) {
      case Opcode::PushConst:
        if (!ins.has_a || ins.a < 0 || ins.a >= int(constants.size()) || !push(constants[ins.a].tag)) return false;
        break;
      case Opcode::Load:
        if (!ins.has_a || ins.a < 0 || ins.a >= n_locals || !view_value_type(state.locals[ins.a]) || !push(state.locals[ins.a])) return false;
        break;
      case Opcode::Store:
        if (!ins.has_a || ins.a < 0 || ins.a >= n_locals || state.sp < 1) return false;
        state.locals[ins.a] = state.stack[--state.sp]; break;
      case Opcode::Neg:
        if (!pop(ValueTag::Int) || !push(ValueTag::Int)) return false;
        break;
      case Opcode::Not:
        if (!pop(ValueTag::Bool) || !push(ValueTag::Bool)) return false;
        break;
      case Opcode::Add: case Opcode::Sub: case Opcode::Mul: case Opcode::Mod:
        if (!pop(ValueTag::Int) || !pop(ValueTag::Int) || !push(ValueTag::Int)) return false;
        break;
      case Opcode::Lt: case Opcode::Le: case Opcode::Gt: case Opcode::Ge:
        if (!pop(ValueTag::Int) || !pop(ValueTag::Int) || !push(ValueTag::Bool)) return false;
        break;
      case Opcode::Eq: case Opcode::Ne: {
        if (state.sp < 2) return false;
        const auto type = state.stack[--state.sp];
        if ((type != ValueTag::Int && type != ValueTag::Bool) || !pop(type) || !push(ValueTag::Bool)) return false;
        break;
      }
      case Opcode::Jmp:
        if (!ins.has_a || ins.a <= int(ip) || !merge(ins.a, state)) return false;
        continue;
      case Opcode::JmpIfFalse: case Opcode::JmpIfTrue:
        if (!pop(ValueTag::Bool) || !ins.has_a || ins.a <= int(ip) || !merge(ins.a, state)) return false;
        break;
      case Opcode::CallBuiltin: {
        if (!ins.has_a || !ins.has_b) return false;
        const auto bid = static_cast<BuiltinId>(ins.a);
        if (bid == BuiltinId::IDiv0 || bid == BuiltinId::IMod0 || bid == BuiltinId::Min || bid == BuiltinId::Max) {
          if (ins.b != 2 || !pop(ValueTag::Int) || !pop(ValueTag::Int) || !push(ValueTag::Int)) return false;
        } else if (bid == BuiltinId::Clip) {
          if (ins.b != 3 || !pop(ValueTag::Int) || !pop(ValueTag::Int) || !pop(ValueTag::Int) || !push(ValueTag::Int)) return false;
        } else if (bid == BuiltinId::Len) {
          if (ins.b != 1 || !pop(ValueTag::IntList) || !push(ValueTag::Int)) return false;
        } else if (bid == BuiltinId::Index) {
          if (ins.b != 2 || !pop(ValueTag::Int) || !pop(ValueTag::IntList) || !push(ValueTag::Int)) return false;
        } else if (bid == BuiltinId::Slice) {
          if (ins.b != 3 || !pop(ValueTag::Int) || !pop(ValueTag::Int) || !pop(ValueTag::IntList) || !push(ValueTag::IntList)) return false;
        } else return false;
        break;
      }
      case Opcode::CheckList:
        if (state.sp < 1 || state.stack[state.sp - 1] != ValueTag::IntList) return false;
        break;
      case Opcode::CheckInt:
        if (state.sp < 1 || state.stack[state.sp - 1] != ValueTag::Int) return false;
        break;
      case Opcode::BoundedRegion: {
        if (!regions || !ins.has_a || ins.a < 0 || ins.a >= int(regions->size())) return false;
        const auto& segment = (*regions)[ins.a];
        const auto& plan = segment.plan;
        for (unsigned i = 0; i < plan.bound_operand_count; ++i) if (!pop(ValueTag::Int)) return false;
        for (auto it = plan.state_types.rbegin(); it != plan.state_types.rend(); ++it) if (!pop(*it)) return false;
        for (std::size_t i = 0; i < segment.parameter_locals.size(); ++i) {
          const auto local = segment.parameter_locals[i];
          if (local < 0 || local >= n_locals || state.locals[local] != plan.parameter_types[i]) return false;
        }
        if (!push(plan.result_type)) return false;
        break;
      }
      case Opcode::Return:
        if (state.sp < 1 || !result_type(state.stack[state.sp - 1])) return false;
        returns = true; continue;
      default: return false;
    }
    if (!merge(ip + 1, state)) return false;
  }
  if (states.back().reached) {
    const auto& last = states.back();
    if (last.sp != 1 || !result_type(last.stack[0])) return false;
    returns = true;
  }
  return returns;
}

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
  for (const auto& segment : program.bounded_region_segments) {
    const auto& plan = segment.plan;
    for (auto type : plan.state_types) if (!view_value_type(type)) return false;
    for (auto type : plan.parameter_types) if (!view_value_type(type)) return false;
    if (!view_value_type(plan.result_type)) return false;
    if (plan.progress == RegionProgressKind::SequenceWindows && plan.state_types[plan.sequence_state] != ValueTag::IntList) return false;
    auto phase_ok = [&](const RegionPhase& phase, ValueTag result) {
      std::array<ValueTag, 64> locals; locals.fill(ValueTag::Invalid);
      for (const auto& binding : phase.bindings) {
        if (binding.local < 0 || binding.local >= 64) return false;
        ValueTag type = ValueTag::Invalid;
        switch (binding.source.bank) {
          case RegionSlotBank::State: type = plan.state_types.at(binding.source.slot); break;
          case RegionSlotBank::Parameter: type = plan.parameter_types.at(binding.source.slot); break;
          case RegionSlotBank::Prepared: type = plan.preparations.at(binding.source.slot).type; break;
          case RegionSlotBank::Result: type = plan.result_type; break;
          case RegionSlotBank::Measure: type = ValueTag::Int; break;
        }
        if (!view_value_type(type)) return false;
        locals[binding.local] = type;
      }
      return view_code_supported(phase.program.code, phase.program.consts, phase.program.n_locals, locals, result);
    };
    if ((segment.boundary && !phase_ok(*segment.boundary, plan.result_type)) ||
        !phase_ok(segment.base_predicate, ValueTag::Bool) || !phase_ok(segment.base_body, plan.result_type) ||
        !phase_ok(segment.combine, plan.result_type)) return false;
    for (std::size_t i = 0; i < segment.preparations.size(); ++i)
      if (!phase_ok(segment.preparations[i], plan.preparations[i].type)) return false;
    for (std::size_t i = 0; i < segment.request_expressions.size(); ++i)
      if (!phase_ok(segment.request_expressions[i], plan.request_expression_types[i])) return false;
  }
  return view_code_supported(program.code, program.consts, program.n_locals, input_types, ValueTag::Invalid, &program.bounded_region_segments);
}
}  // namespace gagp::gpu_detail
