#pragma once

// Host type-flow proof guarantees the instruction/type subset and stack bounds.
// Keep ordinary numeric conversion, protected arithmetic, and every fuel charge.
// This execution representation stores proven scalar/view payloads without tags.
__device__ __noinline__ DResult d_run_view_phase(
    const DCodeView& view, const DLocalPreset* presets, int preset_count,
    const DPayloadTables& tables, int& fuel, ValueTag expected) {
  long long stack[16], locals[8];
  unsigned local_set = 0;
  for (int i = 0; i < preset_count; ++i) {
    const int local = presets[i].local;
    if (local < 0 || local >= 8) return d_error(ErrCode::Name);
    Value value = presets[i].value;
    if (!d_convert_list_view(value, tables)) return d_error(ErrCode::Value);
    locals[local] = value.tag == ValueTag::Bool ? value.b : value.i;
    local_set |= 1u << local;
  }
  int sp = 0, ip = 0;
  while (ip < view.code_len) {
    const auto ins = view.code[ip++];
    if (fuel < 0 || ins.fuel > static_cast<unsigned>(fuel)) return d_error(ErrCode::Timeout);
    fuel -= static_cast<int>(ins.fuel);
    switch (ins.op) {
      case OP_PUSH_CONST: {
        Value value = view.consts[ins.a];
        if (!d_convert_list_view(value, tables)) return d_error(ErrCode::Value);
        stack[sp++] = value.tag == ValueTag::Bool ? value.b : value.i;
        break;
      }
      case OP_LOAD:
        if (!(local_set & (1u << ins.a))) return d_error(ErrCode::Name);
        stack[sp++] = locals[ins.a]; break;
      case OP_STORE:
        locals[ins.a] = stack[--sp]; local_set |= 1u << ins.a; break;
      case OP_NEG: stack[sp - 1] = vm_semantics::wrap_int_neg(stack[sp - 1]); break;
      case OP_NOT: stack[sp - 1] = !stack[sp - 1]; break;
      case OP_ADD: case OP_SUB: case OP_MUL: case OP_MOD: {
        const auto right = stack[--sp]; const auto left = stack[sp - 1];
        const auto a = static_cast<long long>(static_cast<double>(left));
        const auto b = static_cast<long long>(static_cast<double>(right));
        if (ins.op == OP_ADD) stack[sp - 1] = vm_semantics::wrap_int_add(a, b);
        else if (ins.op == OP_SUB) stack[sp - 1] = vm_semantics::wrap_int_sub(a, b);
        else if (ins.op == OP_MUL) stack[sp - 1] = vm_semantics::wrap_int_mul(a, b);
        else { if (right == 0) return d_error(ErrCode::ZeroDiv); stack[sp - 1] = vm_semantics::py_int_mod(a, b); }
        break;
      }
      case OP_EQ: case OP_NE: case OP_LT: case OP_LE: case OP_GT: case OP_GE: {
        const auto b = stack[--sp]; const auto a = stack[sp - 1];
        bool value = false;
        if (ins.op == OP_EQ) value = a == b;
        else if (ins.op == OP_NE) value = a != b;
        else if (ins.op == OP_LT) value = static_cast<double>(a) < static_cast<double>(b);
        else if (ins.op == OP_LE) value = static_cast<double>(a) <= static_cast<double>(b);
        else if (ins.op == OP_GT) value = static_cast<double>(a) > static_cast<double>(b);
        else value = static_cast<double>(a) >= static_cast<double>(b);
        stack[sp - 1] = value; break;
      }
      case OP_JMP: ip = ins.a; break;
      case OP_JMP_IF_TRUE: if (stack[--sp]) ip = ins.a; break;
      case OP_JMP_IF_FALSE: if (!stack[--sp]) ip = ins.a; break;
      case OP_CHECK_INT: case OP_CHECK_LIST: break;
      case OP_CALL_BUILTIN: {
        const auto bid = static_cast<BuiltinId>(ins.a);
        if (bid == BuiltinId::Len) {
          stack[sp - 1] = static_cast<std::uint64_t>(stack[sp - 1]) >> 48;
        } else if (bid == BuiltinId::Index) {
          const auto index = stack[--sp]; const auto source = static_cast<std::uint64_t>(stack[sp - 1]);
          long long normalized;
          if (!d_norm_index_idx(index, source >> 48, normalized)) return d_error(ErrCode::Value);
          const auto value = tables.list_values[(source & Value::k_container_hash_mask) + normalized];
          if (value.tag != ValueTag::Int) return d_error(ErrCode::Type);
          stack[sp - 1] = value.i;
        } else if (bid == BuiltinId::Slice) {
          const auto hi = stack[--sp], lo = stack[--sp];
          const auto source = static_cast<std::uint64_t>(stack[sp - 1]);
          const auto a = d_norm_slice_idx(lo, source >> 48), b = d_norm_slice_idx(hi, source >> 48);
          stack[sp - 1] = Value::pack_container_payload((source & Value::k_container_hash_mask) + a, b > a ? b - a : 0);
        } else if (bid == BuiltinId::Clip) {
          const auto hi = stack[--sp], lo = stack[--sp], value = stack[sp - 1];
          const auto lower = value > lo ? value : lo;
          stack[sp - 1] = lower < hi ? lower : hi;
        } else {
          const auto b = stack[--sp], a = stack[sp - 1];
          if (bid == BuiltinId::IDiv0) stack[sp - 1] = b == 0 ? 0 : vm_semantics::wrap_int_div(a, b);
          else if (bid == BuiltinId::IMod0) stack[sp - 1] = b == 0 ? 0 : vm_semantics::py_int_mod(a, b);
          else {
            const double x = a, y = b;
            stack[sp - 1] = static_cast<long long>(bid == BuiltinId::Min ? (x <= y ? x : y) : (x >= y ? x : y));
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
