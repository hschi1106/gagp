#pragma once

#include "gagp/core/value_semantics.hpp"
#include "region_adapter.hpp"
#include "list_views.hpp"

namespace gagp::detail {

inline std::optional<Value> bound_phase_value(const BoundPhaseInputs& bound, int local) {
  const auto* binding = bound.bindings.locals[local];
  if (!binding) return std::nullopt;
  const auto slot = binding->source.slot;
  switch (binding->source.bank) {
    case RegionSlotBank::State: return bound.frame.state[slot];
    case RegionSlotBank::Prepared: return bound.frame.prepared[slot];
    case RegionSlotBank::Result: return bound.frame.results[slot];
    case RegionSlotBank::Parameter: return bound.parameters[slot];
    case RegionSlotBank::Measure:
      return Value::from_int(Value::container_len(bound.frame.state[bound.plan.sequence_state]));
  }
  return std::nullopt;
}

// Immutable session capability proves forward control flow, stack<=64 and
// Int/Bool/IntList instructions. Runtime mismatches decline BEFORE charging or
// reporting errors, leaving the generic interpreter's lazy error order intact.
inline std::optional<ExecResult> run_typed_phase(const PhaseProgram& program,
                                               const BoundPhaseInputs& bound, int& fuel) {
  if (!bound.bindings.typed) return std::nullopt;
  std::int64_t locals[64], stack[64];
  std::uint64_t local_set = 0;
  for (int i = 0; i < program.n_locals; ++i) {
    const auto value = bound_phase_value(bound, i);
    if (!value) continue;
    if (value->tag != bound.bindings.types[i]) return std::nullopt;
    if (value->tag == ValueTag::IntList && (!active_cpu_list_views || !value->b)) return std::nullopt;
    locals[i] = value->tag == ValueTag::Bool ? value->b : value->i;
    local_set |= UINT64_C(1) << i;
  }
  const int original_fuel = fuel;
  int sp = 0;
  std::size_t ip = 0;
  while (ip < program.code.size()) {
    const auto charge = program.instruction_fuel.empty() ? 1u : program.instruction_fuel[ip];
    if (fuel < 0 || charge > static_cast<unsigned>(fuel))
      return region_failure(ErrCode::Timeout, "out of fuel");
    fuel -= static_cast<int>(charge);
    const auto& ins = program.code[ip++];
    switch (ins.op) {
      case Opcode::PushConst: {
        const auto value = program.consts[ins.a];
        stack[sp++] = value.tag == ValueTag::Bool ? value.b : value.i;
        break;
      }
      case Opcode::Load:
        if (!(local_set & (UINT64_C(1) << ins.a)))
          return region_failure(ErrCode::Name, "read of uninitialized local");
        stack[sp++] = locals[ins.a]; break;
      case Opcode::Neg: stack[sp-1] = vm_semantics::wrap_int_neg(stack[sp-1]); break;
      case Opcode::Not: stack[sp-1] = !stack[sp-1]; break;
      case Opcode::Add: case Opcode::Sub: case Opcode::Mul: case Opcode::Mod: {
        const auto right = stack[--sp], left = stack[sp-1];
        // Preserve the reference VM's double round-trip, including its loss of
        // precision above 2^53; this is not a new direct-integer semantic mode.
        if (static_cast<double>(left) >= 0x1p63 || static_cast<double>(right) >= 0x1p63) {
          fuel = original_fuel; return std::nullopt;
        }
        const auto a = static_cast<long long>(static_cast<double>(left));
        const auto b = static_cast<long long>(static_cast<double>(right));
        if (ins.op == Opcode::Add) stack[sp-1] = vm_semantics::wrap_int_add(a,b);
        else if (ins.op == Opcode::Sub) stack[sp-1] = vm_semantics::wrap_int_sub(a,b);
        else if (ins.op == Opcode::Mul) stack[sp-1] = vm_semantics::wrap_int_mul(a,b);
        else {
          if (!right) return region_failure(ErrCode::ZeroDiv,"modulo by zero");
          stack[sp-1] = vm_semantics::py_int_mod(a,b);
        }
        break;
      }
      case Opcode::Eq: case Opcode::Ne: case Opcode::Lt: case Opcode::Le:
      case Opcode::Gt: case Opcode::Ge: {
        const auto b=stack[--sp], a=stack[sp-1];
        if (ins.op==Opcode::Eq) stack[sp-1]=a==b;
        else if(ins.op==Opcode::Ne) stack[sp-1]=a!=b;
        else if(ins.op==Opcode::Lt) stack[sp-1]=static_cast<double>(a)<static_cast<double>(b);
        else if(ins.op==Opcode::Le) stack[sp-1]=static_cast<double>(a)<=static_cast<double>(b);
        else if(ins.op==Opcode::Gt) stack[sp-1]=static_cast<double>(a)>static_cast<double>(b);
        else stack[sp-1]=static_cast<double>(a)>=static_cast<double>(b);
        break;
      }
      case Opcode::Jmp: ip=ins.a; break;
      case Opcode::JmpIfTrue: if(stack[--sp]) ip=ins.a; break;
      case Opcode::JmpIfFalse: if(!stack[--sp]) ip=ins.a; break;
      case Opcode::CheckInt: case Opcode::CheckList: break;
      case Opcode::CallBuiltin: {
        const auto id=static_cast<BuiltinId>(ins.a);
        if(id==BuiltinId::Len) stack[sp-1]=static_cast<std::uint64_t>(stack[sp-1])>>48;
        else if(id==BuiltinId::Index || id==BuiltinId::Slice) {
          const auto hi=stack[--sp];
          const auto lo=id==BuiltinId::Slice ? stack[--sp] : hi;
          const auto source=static_cast<std::uint64_t>(stack[sp-1]);
          const auto length=source>>48, offset=source & Value::k_container_hash_mask;
          if(!active_cpu_list_views || offset>active_cpu_list_views->values.size() ||
              length>active_cpu_list_views->values.size()-offset)
            return region_failure(ErrCode::Value,"list view exceeds invocation storage");
          if(id==BuiltinId::Index) {
            auto index=lo; if(index<0) index+=static_cast<std::int64_t>(length);
            if(index<0 || static_cast<std::uint64_t>(index)>=length)
              return region_failure(ErrCode::Value,"index out of range");
            stack[sp-1]=active_cpu_list_views->values[offset+index].i;
          } else {
            const auto a=CpuListViews::slice_index(lo,length), b=CpuListViews::slice_index(hi,length);
            stack[sp-1]=Value::pack_container_payload(offset+a,b>a?b-a:0);
          }
        } else if(id==BuiltinId::Clip) {
          const auto hi=stack[--sp], lo=stack[--sp], value=stack[sp-1];
          stack[sp-1]=std::min(std::max(value,lo),hi);
        } else {
          const auto b=stack[--sp], a=stack[sp-1];
          if(id==BuiltinId::IDiv0) stack[sp-1]=b?vm_semantics::wrap_int_div(a,b):0;
          else if(id==BuiltinId::IMod0) stack[sp-1]=b?vm_semantics::py_int_mod(a,b):0;
          else {
            const double x=a,y=b;
            const double picked=id==BuiltinId::Min?(x<=y?x:y):(x>=y?x:y);
            if(picked>=0x1p63) { fuel=original_fuel; return std::nullopt; }
            stack[sp-1]=static_cast<long long>(picked);
          }
        }
        break;
      }
      case Opcode::Return: ip=program.code.size(); break;
      default: return region_failure(ErrCode::Value,"unsupported typed phase instruction");
    }
  }
  Value value;
  if(bound.bindings.result_type==ValueTag::Bool) value=Value::from_bool(stack[sp-1]);
  else if(bound.bindings.result_type==ValueTag::IntList) {
    const auto packed=static_cast<std::uint64_t>(stack[sp-1]);
    value=Value::from_int_list_hash_len(packed & Value::k_container_hash_mask,packed>>48);
    value.b=true;
  } else value=Value::from_int(stack[sp-1]);
  return ExecResult{false,value,{ErrCode::Value,""}};
}
} // namespace gagp::detail
