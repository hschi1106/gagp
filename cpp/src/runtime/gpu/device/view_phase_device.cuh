#pragma once

#include "phase_register_bank.cuh"

// Host type-flow proof guarantees the instruction/type subset and stack bounds.
// Keep ordinary numeric conversion, protected arithmetic, and every fuel charge.
// This execution representation stores proven scalar/view payloads without tags.
struct DViewPhasePayloadOps {
  __device__ bool convert(Value& value,const DPayloadTables& tables) const {
    return d_convert_list_view(value,tables);
  }
  template<class Stack> __device__ __forceinline__ bool call(BuiltinId,Stack&,int&,const DPayloadTables&,DResult&) const {return false;}
};

template<DPayloadFlavor Flavor>
struct DPrivatePhasePayloadOps {
  typename DPayloadFlavorTraits<Flavor>::State& state;
  __device__ bool convert(Value&,const DPayloadTables&) const {return true;}
  template<class Stack> __device__ __forceinline__ bool call(BuiltinId bid,Stack& stack,int& sp,
      const DPayloadTables& tables,DResult& result) const {
    int count=0;
    switch(bid) {
      case BuiltinId::Singleton:count=1;break;
      case BuiltinId::Concat:case BuiltinId::Index:case BuiltinId::IndexNonnegative:
      case BuiltinId::IndexClampedOrZero:count=2;break;
      case BuiltinId::Slice:count=3;break;
      case BuiltinId::Len:case BuiltinId::IsLetter:case BuiltinId::Clip:
      case BuiltinId::TruncDiv0:case BuiltinId::TruncMod0:case BuiltinId::IDiv0:
      case BuiltinId::IMod0:case BuiltinId::Min:case BuiltinId::Max:return false;
      default:result=d_error(ErrCode::Name);return true;
    }
    Value args[3];
    for(int i=0;i<count;++i) {
      args[i]=Value::from_int(stack[sp-count+i]);
      if(bid!=BuiltinId::Singleton && (i==0 || (bid==BuiltinId::Concat && i==1))) {
        args[i].tag=ValueTag::IntList;args[i].b=true;
      }
    }
    Value value;ErrCode error=ErrCode::Value;
    if(!d_builtin_call<Flavor>(bid,args,count,tables,state,value,error))result=d_error(error);
    else {sp-=count-1;stack[sp-1]=value.i;}
    return true;
  }
};

struct DViewPresets {
  const DLocalPreset* presets;
  int preset_count;
  template<class Locals> __device__ __forceinline__ ErrCode init(Locals& locals,unsigned& local_set,
      const DPayloadTables& tables,unsigned&) const {
  for (int i = 0; i < preset_count; ++i) {
    const int local = presets[i].local;
    if (local < 0 || local >= 8) return ErrCode::Name;
    Value value = presets[i].value;
    if (!d_convert_list_view(value, tables)) return ErrCode::Value;
    locals[local] = value.tag == ValueTag::Bool ? value.b : value.i;
    local_set |= 1u << local;
  }
    return static_cast<ErrCode>(-1);
  }
};

template <class Binder,class PayloadOps=DViewPhasePayloadOps,class Stack=DPhaseArrayBank<16>,class Locals=DPhaseArrayBank<8>>
__device__ __noinline__ DResult d_run_bound_view_phase(
    const DCodeView& view, Binder binder,
    const DPayloadTables& tables, int& fuel, ValueTag expected, PayloadOps payload_ops={}) {
  Stack stack;Locals locals;
  unsigned local_set=0,wrong_type=0;
  const auto error=binder.init(locals,local_set,tables,wrong_type);
  if (static_cast<int>(error) != -1) return d_error(error);
  int sp = 0, ip = 0;
  while (ip < view.code_len) {
    const auto ins = view.code[ip++];
    if (fuel < 0 || ins.fuel > static_cast<unsigned>(fuel)) return d_error(ErrCode::Timeout);
    fuel -= static_cast<int>(ins.fuel);
    switch (ins.op) {
      case OP_PUSH_CONST: {
        Value value = view.consts[ins.a];
        if (!payload_ops.convert(value,tables)) return d_error(ErrCode::Value);
        stack[sp++] = value.tag == ValueTag::Bool ? value.b : value.i;
        break;
      }
      case OP_LOAD:
        if (!(local_set & (1u << ins.a))) return d_error(ErrCode::Name);
        if(wrong_type & (1u<<ins.a))return d_error(ErrCode::Type);
        stack[sp++] = locals[ins.a]; break;
      case OP_STORE:
        locals[ins.a] = stack[--sp]; local_set |= 1u << ins.a;
        wrong_type &= ~(1u<<ins.a);break;
      case OP_NEG: stack[sp - 1] = vm_semantics::wrap_int_neg(stack[sp - 1]); break;
      case OP_NOT: stack[sp - 1] = !stack[sp - 1]; break;
      case OP_ADD: case OP_SUB: case OP_MUL: case OP_MOD: {
        const long long right = stack[--sp]; const long long left = stack[sp - 1];
        const auto a = left;
        const auto b = right;
        if (ins.op == OP_ADD) stack[sp - 1] = vm_semantics::wrap_int_add(a, b);
        else if (ins.op == OP_SUB) stack[sp - 1] = vm_semantics::wrap_int_sub(a, b);
        else if (ins.op == OP_MUL) stack[sp - 1] = vm_semantics::wrap_int_mul(a, b);
        else { if (right == 0) return d_error(ErrCode::ZeroDiv); stack[sp - 1] = d_py_int_mod(a, b); }
        break;
      }
      case OP_EQ: case OP_NE: case OP_LT: case OP_LE: case OP_GT: case OP_GE: {
        const long long b = stack[--sp]; const long long a = stack[sp - 1];
        bool value = false;
        if (ins.op == OP_EQ) value = a == b;
        else if (ins.op == OP_NE) value = a != b;
        else if (ins.op == OP_LT) value = a < b;
        else if (ins.op == OP_LE) value = a <= b;
        else if (ins.op == OP_GT) value = a > b;
        else value = a >= b;
        stack[sp - 1] = value; break;
      }
      case OP_JMP: ip = ins.a; break;
      case OP_JMP_IF_TRUE: if (stack[--sp]) ip = ins.a; break;
      case OP_JMP_IF_FALSE: if (!stack[--sp]) ip = ins.a; break;
      case OP_CHECK_INT: case OP_CHECK_LIST: break;
      case OP_CALL_BUILTIN: {
        const auto bid = static_cast<BuiltinId>(ins.a);
        DResult result;
        if(payload_ops.call(bid,stack,sp,tables,result)) {
          if(result.is_error)return result;
          break;
        }
        if (bid == BuiltinId::IsLetter) {
          const auto x=stack[sp-1]; stack[sp-1]=(x>='A' && x<='Z') || (x>='a' && x<='z');
        } else if (bid == BuiltinId::Len) {
          stack[sp - 1] = static_cast<std::uint64_t>(stack[sp - 1]) >> 48;
        } else if (bid == BuiltinId::Index || bid == BuiltinId::IndexNonnegative || bid == BuiltinId::IndexClampedOrZero) {
          const long long index = stack[--sp]; const auto source = static_cast<std::uint64_t>(stack[sp - 1]);
          long long normalized;
          if (bid == BuiltinId::IndexClampedOrZero) {
            const auto length = source >> 48;
            if (!length) { stack[sp-1]=0; break; }
            normalized = index < 0 ? 0 : (static_cast<std::uint64_t>(index) >= length ? length-1 : index);
          } else if ((bid == BuiltinId::IndexNonnegative && index < 0) || !d_norm_index_idx(index, source >> 48, normalized)) return d_error(ErrCode::Value);
          const auto value = tables.list_values[(source & Value::k_container_hash_mask) + normalized];
          if (value.tag != ValueTag::Int) return d_error(ErrCode::Type);
          stack[sp - 1] = value.i;
        } else if (bid == BuiltinId::Slice) {
          const long long hi = stack[--sp], lo = stack[--sp];
          const auto source = static_cast<std::uint64_t>(stack[sp - 1]);
          const auto a = d_norm_slice_idx(lo, source >> 48), b = d_norm_slice_idx(hi, source >> 48);
          stack[sp - 1] = Value::pack_container_payload((source & Value::k_container_hash_mask) + a, b > a ? b - a : 0);
        } else if (bid == BuiltinId::Clip) {
          const long long hi = stack[--sp], lo = stack[--sp], value = stack[sp - 1];
          const auto lower = value > lo ? value : lo;
          stack[sp - 1] = lower < hi ? lower : hi;
        } else {
          const long long b = stack[--sp], a = stack[sp - 1];
          if (bid == BuiltinId::TruncDiv0) stack[sp-1]=d_trunc_int_div0(a,b);
          else if (bid == BuiltinId::TruncMod0) stack[sp-1]=d_trunc_int_mod0(a,b);
          else if (bid == BuiltinId::IDiv0) stack[sp - 1] = b == 0 ? 0 : d_wrap_int_div(a, b);
          else if (bid == BuiltinId::IMod0) stack[sp - 1] = b == 0 ? 0 : d_py_int_mod(a, b);
          else {
            stack[sp - 1] = bid == BuiltinId::Min ? (a <= b ? a : b) : (a >= b ? a : b);
          }
        }
        break;
      }
      case OP_RETURN: ip = view.code_len; break;
      default: return d_error(ErrCode::Type);
    }
  }
  if (sp < 1) return d_error(ErrCode::Value);
  if (expected == ValueTag::Bool) return d_ok(Value::from_bool(stack[sp - 1] != 0));
  if (expected == ValueTag::IntList) {
    Value value = Value::from_int_list_hash_len(static_cast<std::uint64_t>(stack[sp - 1]) & Value::k_container_hash_mask,
                                              static_cast<std::uint64_t>(stack[sp - 1]) >> 48);
    value.b = true; return d_ok(value);
  }
  return d_ok(Value::from_int(stack[sp - 1]));
}

__device__ inline DResult d_run_view_phase(const DCodeView& view,
    const DLocalPreset* presets, int preset_count, const DPayloadTables& tables,
    int& fuel, ValueTag expected) {
  return d_run_bound_view_phase(view, DViewPresets{presets, preset_count}, tables, fuel, expected);
}
