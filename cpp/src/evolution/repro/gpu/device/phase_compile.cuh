#pragma once
#include "gagp/runtime/gpu/device_types_gpu.hpp"
#include "gagp/core/builtin.hpp"

namespace gagp::evo::repro {
// Lowering prototype only: this proves bounded, typed phase bytecode. It does
// NOT establish grammar membership. Callers must separately admit the source.
enum class PhaseTreeKind : int { Constant, Local, Operation, If, And, Or };
struct PhaseTreeNode {
  PhaseTreeKind kind = PhaseTreeKind::Constant;
  Opcode opcode = Opcode::PushConst;
  int operand = 0;
  int arity = 0;
  unsigned fuel = 1;
  unsigned merge_fuel = 1;
};
enum class PhaseCompileStatus : int { Ok, Invalid, Unsupported, Capacity };
struct PhaseCompileResult {
  PhaseCompileStatus status = PhaseCompileStatus::Invalid;
  int code_count = 0;
  int constant_count = 0;
  int stack_bound = 0;
  ValueTag result_type = ValueTag::Invalid;
  // The closed operation_type set, typed constants/locals and forward typed
  // branches also prove the runtime view_code_supported capability. Any future
  // extension must retain that proof or return false here.
  bool typed_views_proven = false;
};
namespace phase_compile_detail {
using gpu_detail::DInstr;
struct Output {
  DInstr* code;
  Value* constants;
  int code_capacity, constant_capacity;
  PhaseCompileResult result;
  int stack = 0;
  __device__ bool emit(Opcode op, unsigned fuel, int a=0, int b=0, unsigned flags=0) {
    if (result.code_count >= code_capacity) { result.status=PhaseCompileStatus::Capacity; return false; }
    if (fuel > 0x7fffffffU) { result.status=PhaseCompileStatus::Invalid; return false; }
    code[result.code_count++] = {static_cast<std::uint8_t>(op),static_cast<std::uint8_t>(flags),a,b,fuel};
    return true;
  }
  __device__ bool push(Value value, unsigned fuel) {
    if (result.constant_count >= constant_capacity) { result.status=PhaseCompileStatus::Capacity; return false; }
    const int index=result.constant_count++;
    constants[index]=value;
    if (!emit(Opcode::PushConst,fuel,index,0,gpu_detail::DINSTR_HAS_A)) return false;
    ++stack; if (stack>result.stack_bound) result.stack_bound=stack;
    return true;
  }
};
__device__ inline ValueTag operation_type(const PhaseTreeNode& node, const ValueTag* args) {
  const auto integer=ValueTag::Int, boolean=ValueTag::Bool, list=ValueTag::IntList;
  if (node.opcode==Opcode::Neg || node.opcode==Opcode::CheckInt)
    return node.arity==1 && args[0]==integer ? integer : ValueTag::Invalid;
  if (node.opcode==Opcode::Not)
    return node.arity==1 && args[0]==boolean ? boolean : ValueTag::Invalid;
  if (node.opcode==Opcode::CheckList)
    return node.arity==1 && args[0]==list ? list : ValueTag::Invalid;
  switch (node.opcode) {
    case Opcode::Add: case Opcode::Sub: case Opcode::Mul: case Opcode::Mod:
      return node.arity==2 && args[0]==integer && args[1]==integer ? integer : ValueTag::Invalid;
    case Opcode::Lt: case Opcode::Le: case Opcode::Gt: case Opcode::Ge:
      return node.arity==2 && args[0]==integer && args[1]==integer ? boolean : ValueTag::Invalid;
    case Opcode::Eq: case Opcode::Ne:
      return node.arity==2 && args[0]==args[1] && (args[0]==integer || args[0]==boolean) ? boolean : ValueTag::Invalid;
    case Opcode::CallBuiltin: break;
    default: return ValueTag::Invalid;
  }
  switch (static_cast<BuiltinId>(node.operand)) {
    case BuiltinId::IDiv0: case BuiltinId::IMod0: case BuiltinId::Min: case BuiltinId::Max:
      return node.arity==2 && args[0]==integer && args[1]==integer ? integer : ValueTag::Invalid;
    case BuiltinId::Clip:
      return node.arity==3 && args[0]==integer && args[1]==integer && args[2]==integer ? integer : ValueTag::Invalid;
    case BuiltinId::Len: return node.arity==1 && args[0]==list ? integer : ValueTag::Invalid;
    case BuiltinId::Index: return node.arity==2 && args[0]==list && args[1]==integer ? integer : ValueTag::Invalid;
    case BuiltinId::Slice: return node.arity==3 && args[0]==list && args[1]==integer && args[2]==integer ? list : ValueTag::Invalid;
    default: return ValueTag::Invalid;
  }
}
}  // namespace phase_compile_detail

// One thread per phase. All cursors/output writes are capacity checked; depth is
// bounded independently of input metadata. Invalid/unsupported output is never
// executable. No device allocation, recursive C++ call, or run-time JIT occurs.
__device__ inline PhaseCompileResult compile_phase_tree(
    const PhaseTreeNode* nodes, int node_count,
    const Value* constants, int constant_count,
    const ValueTag* local_types, int local_count, ValueTag expected,
    gpu_detail::DInstr* code, int code_capacity, Value* output_constants, int constant_capacity) {
  using namespace phase_compile_detail;
  Output out{code,output_constants,code_capacity,constant_capacity,{}};
  if (!nodes || !code || !output_constants || node_count<=0 || node_count>4096 ||
      constant_count<0 || (constant_count && !constants) || local_count<0 || local_count>64 ||
      (local_count && !local_types) || code_capacity<=0 || constant_capacity<=0) return out.result;
  struct Frame { PhaseTreeNode node; int seen, base_stack, first_jump, second_jump; ValueTag args[3]; };
  Frame frames[64]; int depth=0, cursor=0;
  ValueTag completed=ValueTag::Invalid;
  bool have_value=false;
  for (;;) {
    if (!have_value) {
      if (cursor>=node_count) return out.result;
      const auto node=nodes[cursor++];
      if (node.fuel>0x7fffffffU || node.merge_fuel>0x7fffffffU) return out.result;
      if (node.kind==PhaseTreeKind::Constant) {
        if (node.arity!=0 || node.operand<0 || node.operand>=constant_count) return out.result;
        const auto value=constants[node.operand];
        if (value.tag!=ValueTag::Int && value.tag!=ValueTag::Bool) { out.result.status=PhaseCompileStatus::Unsupported; return out.result; }
        if (!out.push(value,node.fuel)) return out.result;
        completed=value.tag; have_value=true;
      } else if (node.kind==PhaseTreeKind::Local) {
        if (node.arity!=0 || node.operand<0 || node.operand>=local_count) return out.result;
        completed=local_types[node.operand];
        if (completed!=ValueTag::Int && completed!=ValueTag::Bool && completed!=ValueTag::IntList) { out.result.status=PhaseCompileStatus::Unsupported; return out.result; }
        if (!out.emit(Opcode::Load,node.fuel,node.operand,0,gpu_detail::DINSTR_HAS_A)) return out.result;
        ++out.stack; if (out.stack>out.result.stack_bound) out.result.stack_bound=out.stack;
        have_value=true;
      } else {
        if (node.arity<1 || node.arity>3) return out.result;
        if (node.kind==PhaseTreeKind::If) { if (node.arity!=3) return out.result; }
        else if (node.kind==PhaseTreeKind::And || node.kind==PhaseTreeKind::Or) { if (node.arity!=2) return out.result; }
        else if (node.kind!=PhaseTreeKind::Operation) { out.result.status=PhaseCompileStatus::Unsupported; return out.result; }
        if (depth==64) { out.result.status=PhaseCompileStatus::Capacity; return out.result; }
        frames[depth++]={node,0,out.stack,-1,-1,{}};
        continue;
      }
    }
    if (depth==0) {
      if (cursor!=node_count || out.stack!=1 || completed!=expected) return out.result;
      if (out.result.stack_bound>gpu_detail::MAX_STACK) { out.result.status=PhaseCompileStatus::Capacity; return out.result; }
      out.result.status=PhaseCompileStatus::Ok; out.result.result_type=completed;
      out.result.typed_views_proven=true;
      return out.result;
    }
    auto& frame=frames[depth-1];
    const auto& node=frame.node;
    frame.args[frame.seen++]=completed;
    const bool branch=node.kind==PhaseTreeKind::If || node.kind==PhaseTreeKind::And || node.kind==PhaseTreeKind::Or;
    if (branch && frame.seen==1) {
      if (completed!=ValueTag::Bool) return out.result;
      frame.first_jump=out.result.code_count;
      if (!out.emit(node.kind==PhaseTreeKind::Or ? Opcode::JmpIfTrue : Opcode::JmpIfFalse,
                    node.fuel,0,0,gpu_detail::DINSTR_HAS_A)) return out.result;
      --out.stack;
    } else if (node.kind==PhaseTreeKind::If && frame.seen==2) {
      frame.second_jump=out.result.code_count;
      if (!out.emit(Opcode::Jmp,node.merge_fuel,0,0,gpu_detail::DINSTR_HAS_A)) return out.result;
      out.code[frame.first_jump].a=out.result.code_count;
      out.stack=frame.base_stack;
    }
    if (frame.seen<node.arity) { have_value=false; continue; }
    if (node.kind==PhaseTreeKind::If) {
      if (frame.args[1]!=frame.args[2]) return out.result;
      out.code[frame.second_jump].a=out.result.code_count;
    } else if (node.kind==PhaseTreeKind::And || node.kind==PhaseTreeKind::Or) {
      if (completed!=ValueTag::Bool) return out.result;
      if (!out.emit(Opcode::Not,node.fuel) || !out.emit(Opcode::Not,node.fuel)) return out.result;
      const int finish=out.result.code_count;
      if (!out.emit(Opcode::Jmp,node.fuel,0,0,gpu_detail::DINSTR_HAS_A)) return out.result;
      out.code[frame.first_jump].a=out.result.code_count;
      out.stack=frame.base_stack;
      if (!out.push(Value::from_bool(node.kind==PhaseTreeKind::Or),node.fuel)) return out.result;
      out.code[finish].a=out.result.code_count;
    } else {
      completed=operation_type(node,frame.args);
      if (completed==ValueTag::Invalid) { out.result.status=PhaseCompileStatus::Unsupported; return out.result; }
      if (node.opcode==Opcode::CallBuiltin) {
        if (!out.emit(node.opcode,node.fuel,node.operand,node.arity,gpu_detail::DINSTR_HAS_A|gpu_detail::DINSTR_HAS_B)) return out.result;
      } else if (!out.emit(node.opcode,node.fuel)) return out.result;
      out.stack-=node.arity-1;
    }
    --depth;
  }
}
}  // namespace gagp::evo::repro
