#pragma once

#include <cstdint>

#include "gagp/core/builtin.hpp"
#include "builtins_device.cuh"
#include "gagp/core/value_semantics.hpp"
#include "gagp/runtime/gpu/device_types_gpu.hpp"
#include "gagp/runtime/gpu/region_types_gpu.hpp"

namespace gagp::gpu_detail {

__device__ inline void d_fail(DResult& out, ErrCode code) {
  out.is_error = 1;
  out.err_code = code;
}

__device__ inline ValueTag d_list_tag_from_private_code(int code) {
  if (code == 1) return ValueTag::IntList;
  if (code == 2) return ValueTag::FloatList;
  if (code == 3) return ValueTag::StringList;
  return ValueTag::Invalid;
}

template <typename State>
__device__ inline bool d_make_empty_list_for_tag(ValueTag tag, State& st, Value& out) {
  if (!d_is_typed_list_tag(tag)) {
    return false;
  }
  const std::uint64_t h = d_hash_list_payload(nullptr, 0);
  out = d_make_list_hash_len(tag, h, 0U);
  if constexpr (State::kMaxListEntries > 0) {
    const int off = st.list_values_used;
    (void)d_register_local_list(st, out, off, 0);
  }
  return true;
}

template <DPayloadFlavor Flavor>
struct DPayloadStateStorage {
  typename DPayloadFlavorTraits<Flavor>::State state{};

  __device__ typename DPayloadFlavorTraits<Flavor>::State& ref() { return state; }
};

struct DExecutionTables {
  const DInstr* phase_code = nullptr;
  const Value* phase_consts = nullptr;
  const DRegionSegment* region_segments = nullptr;
  int region_segment_count = 0;
  const DRegionPhase* region_phases = nullptr;
  int region_phase_count = 0;
  const DRegionPhaseBinding* region_bindings = nullptr;
  int region_binding_count = 0;
};

struct DCodeView {
  const DInstr* code = nullptr;
  int code_len = 0;
  const Value* consts = nullptr;
  int const_len = 0;
  int n_locals = 0;
  int region_offset = 0;
  int region_count = 0;
};

struct DLocalPreset {
  int local = -1;
  Value value = Value::invalid();
  ValueTag expected = ValueTag::Invalid;
};

__device__ inline DResult d_ok(Value value) {
  DResult out;
  out.is_error = 0;
  out.err_code = ErrCode::Value;
  out.value = value;
  return out;
}

__device__ inline DResult d_error(ErrCode code) {
  DResult out;
  d_fail(out, code);
  return out;
}

__device__ inline bool d_is_payload_value(const Value& v) {
  return v.tag == ValueTag::String || v.tag == ValueTag::IntList ||
         v.tag == ValueTag::FloatList || v.tag == ValueTag::StringList ||
         v.tag == ValueTag::FallbackToken;
}

template <DPayloadFlavor Flavor, bool EnableRegions = false, int StackCapacity = MAX_STACK, int LocalCapacity = MAX_LOCALS>
__device__ __noinline__ DResult d_run_code_core(const DCodeView& view,
                                                const Value* shared_case_local_vals,
                                                const unsigned char* shared_case_local_set,
                                                int local_case,
                                                const DLocalPreset* presets,
                                                int preset_count,
                                                const DPayloadTables& payload_tables,
                                                typename DPayloadFlavorTraits<Flavor>::State& payload_state,
                                                const DExecutionTables& execution_tables,
                                                int& fuel_left,
                                                bool require_return,
                                                DRegionWorkspace workspace = {});

#include "view_phase_device.cuh"
#include "region_execution_device.cuh"

template <DPayloadFlavor Flavor, bool EnableRegions, int StackCapacity, int LocalCapacity>
__device__ __noinline__ DResult d_run_code_core(const DCodeView& view,
                                                const Value* shared_case_local_vals,
                                                const unsigned char* shared_case_local_set,
                                                int local_case,
                                                const DLocalPreset* presets,
                                                int preset_count,
                                                const DPayloadTables& payload_tables,
                                                typename DPayloadFlavorTraits<Flavor>::State& payload_state,
                                                const DExecutionTables& execution_tables,
                                                int& fuel_left,
                                                bool require_return,
                                                DRegionWorkspace workspace) {
  DResult result;
  result.is_error = 0;
  result.err_code = ErrCode::Value;
  result.value = Value::invalid();

  Value stack[StackCapacity];
  Value locals[LocalCapacity];
  static_assert(LocalCapacity <= 64, "local_set_mask requires LocalCapacity <= 64");
  std::uint64_t local_set_mask = 0;
  std::uint64_t local_type_mask = 0;
  ValueTag local_types[LocalCapacity];

  if (view.n_locals < 0 || view.n_locals > LocalCapacity) {
    return d_error(ErrCode::Value);
  }
  if (shared_case_local_vals != nullptr && shared_case_local_set != nullptr) {
    const int base = local_case * MAX_LOCALS;
    for (int i = 0; i < view.n_locals; ++i) {
      locals[i] = shared_case_local_vals[base + i];
      if (shared_case_local_set[base + i]) {
        local_set_mask |= (std::uint64_t{1} << i);
      }
    }
  }
  for (int i = 0; i < preset_count; ++i) {
    const int local = presets[i].local;
    if (local < 0 || local >= view.n_locals) {
      return d_error(ErrCode::Name);
    }
    locals[local] = presets[i].value;
    local_set_mask |= (std::uint64_t{1} << local);
    if (presets[i].expected != ValueTag::Invalid) {
      local_types[local] = presets[i].expected;
      local_type_mask |= (std::uint64_t{1} << local);
    }
  }

  int sp = 0;
  int ip = 0;
  bool returned = false;

  while (ip < view.code_len) {
    const DInstr ins = view.code[ip];
    if (fuel_left < 0 || ins.fuel > static_cast<std::uint32_t>(fuel_left)) {
      d_fail(result, ErrCode::Timeout);
      break;
    }
    fuel_left -= static_cast<int>(ins.fuel);

    ip += 1;

    if (ins.op == OP_PUSH_CONST) {
      if (!d_has_a(ins) || ins.a < 0 || ins.a >= view.const_len || sp >= StackCapacity) {
        d_fail(result, ErrCode::Value);
        break;
      }
      Value value = view.consts[ins.a];
      if constexpr ((Flavor == DPayloadFlavor::IntListViews || Flavor == DPayloadFlavor::BoundIntListViews)) {
        if (!d_convert_list_view(value, payload_tables)) { d_fail(result, ErrCode::Value); break; }
      }
      stack[sp++] = value;
      continue;
    }

    if (ins.op == OP_LOAD) {
      if (!d_has_a(ins) || ins.a < 0 || ins.a >= view.n_locals || sp >= StackCapacity) {
        d_fail(result, ErrCode::Name);
        break;
      }
      if ((local_set_mask & (std::uint64_t{1} << ins.a)) == 0) {
        d_fail(result, ErrCode::Name);
        break;
      }
      if ((local_type_mask & (std::uint64_t{1} << ins.a)) != 0 &&
          locals[ins.a].tag != local_types[ins.a]) {
        d_fail(result, ErrCode::Type);
        break;
      }
      if constexpr ((Flavor == DPayloadFlavor::IntListViews || Flavor == DPayloadFlavor::BoundIntListViews)) {
        if (!d_convert_list_view(locals[ins.a], payload_tables)) { d_fail(result, ErrCode::Value); break; }
      }
      stack[sp++] = locals[ins.a];
      continue;
    }

    if (ins.op == OP_STORE) {
      if (!d_has_a(ins) || ins.a < 0 || ins.a >= view.n_locals) {
        d_fail(result, ErrCode::Name);
        break;
      }
      if (sp < 1) {
        d_fail(result, ErrCode::Value);
        break;
      }
      locals[ins.a] = stack[--sp];
      local_set_mask |= (std::uint64_t{1} << ins.a);
      local_type_mask &= ~(std::uint64_t{1} << ins.a);
      continue;
    }

    if (ins.op == OP_NEG || ins.op == OP_NOT) {
      if (sp < 1) {
        d_fail(result, ErrCode::Value);
        break;
      }
      Value x = stack[--sp];
      if (ins.op == OP_NEG) {
        if (!d_is_num(x)) {
          d_fail(result, ErrCode::Type);
          break;
        }
        stack[sp++] = (x.tag == ValueTag::Float)
                          ? Value::from_float(vm_semantics::canonicalize_vm_float(-x.f))
                          : Value::from_int(vm_semantics::wrap_int_neg(x.i));
      } else {
        if (x.tag != ValueTag::Bool) {
          d_fail(result, ErrCode::Type);
          break;
        }
        stack[sp++] = Value::from_bool(!x.b);
      }
      continue;
    }

    if (ins.op == OP_ADD || ins.op == OP_SUB || ins.op == OP_MUL || ins.op == OP_DIV ||
        ins.op == OP_MOD) {
      if (sp < 2) {
        d_fail(result, ErrCode::Value);
        break;
      }
      Value b = stack[--sp];
      Value a = stack[--sp];
      double a_num = 0.0;
      double b_num = 0.0;
      bool any_float = false;
      if (!d_to_numeric_pair(a, b, a_num, b_num, any_float)) {
        d_fail(result, ErrCode::Type);
        break;
      }
      if ((ins.op == OP_DIV || ins.op == OP_MOD) && b_num == 0.0) {
        d_fail(result, ErrCode::ZeroDiv);
        break;
      }
      if (ins.op == OP_ADD) {
        stack[sp++] = any_float ? Value::from_float(vm_semantics::canonicalize_vm_float(a_num + b_num))
                                : Value::from_int(vm_semantics::wrap_int_add(
                                      static_cast<long long>(a_num), static_cast<long long>(b_num)));
      } else if (ins.op == OP_SUB) {
        stack[sp++] = any_float ? Value::from_float(vm_semantics::canonicalize_vm_float(a_num - b_num))
                                : Value::from_int(vm_semantics::wrap_int_sub(
                                      static_cast<long long>(a_num), static_cast<long long>(b_num)));
      } else if (ins.op == OP_MUL) {
        stack[sp++] = any_float ? Value::from_float(vm_semantics::canonicalize_vm_float(a_num * b_num))
                                : Value::from_int(vm_semantics::wrap_int_mul(
                                      static_cast<long long>(a_num), static_cast<long long>(b_num)));
      } else if (ins.op == OP_DIV) {
        stack[sp++] = Value::from_float(vm_semantics::canonicalize_vm_float(a_num / b_num));
      } else {
        stack[sp++] = any_float
                          ? Value::from_float(vm_semantics::canonicalize_vm_float(d_float_mod(a_num, b_num)))
                          : Value::from_int(d_int_mod(static_cast<long long>(a_num),
                                                      static_cast<long long>(b_num)));
      }
      continue;
    }

    if (ins.op == OP_LT || ins.op == OP_LE || ins.op == OP_GT || ins.op == OP_GE ||
        ins.op == OP_EQ || ins.op == OP_NE) {
      if (sp < 2) {
        d_fail(result, ErrCode::Value);
        break;
      }
      Value b = stack[--sp];
      Value a = stack[--sp];
      bool cmp = false;
      ErrCode derr = ErrCode::Type;
      if (!d_compare(ins.op, a, b, cmp, derr)) {
        d_fail(result, derr);
        break;
      }
      stack[sp++] = Value::from_bool(cmp);
      continue;
    }

    if (ins.op == OP_JMP) {
      if (!d_has_a(ins) || ins.a < 0 || ins.a > view.code_len) {
        d_fail(result, ErrCode::Value);
        break;
      }
      ip = ins.a;
      continue;
    }

    if (ins.op == OP_JMP_IF_FALSE || ins.op == OP_JMP_IF_TRUE) {
      if (sp < 1) {
        d_fail(result, ErrCode::Value);
        break;
      }
      if (!d_has_a(ins) || ins.a < 0 || ins.a > view.code_len) {
        d_fail(result, ErrCode::Value);
        break;
      }
      Value c = stack[--sp];
      if (c.tag != ValueTag::Bool) {
        d_fail(result, ErrCode::Type);
        break;
      }
      if (ins.op == OP_JMP_IF_FALSE && !c.b) ip = ins.a;
      if (ins.op == OP_JMP_IF_TRUE && c.b) ip = ins.a;
      continue;
    }

    if (ins.op == OP_CALL_BUILTIN) {
      BuiltinId bid = BuiltinId::Abs;
      const int argc = d_has_b(ins) ? ins.b : -1;
      if (!d_has_a(ins) || !builtin_id_from_int(ins.a, bid)) {
        d_fail(result, ErrCode::Name);
        break;
      }
      if (argc < 0 || sp < argc) {
        d_fail(result, (argc < 0) ? ErrCode::Type : ErrCode::Value);
        break;
      }
      Value args_buf[4];
      if (argc > 4) {
        d_fail(result, ErrCode::Type);
        break;
      }
      for (int i = 0; i < argc; ++i) {
        args_buf[i] = stack[sp - argc + i];
      }
      sp -= argc;
      ErrCode derr = ErrCode::Type;
      Value ret = Value::invalid();
      if (!d_builtin_call<Flavor>(
              bid, args_buf, argc, payload_tables, payload_state, ret, derr)) {
        d_fail(result, derr);
        break;
      }
      if (sp >= StackCapacity) {
        d_fail(result, ErrCode::Value);
        break;
      }
      stack[sp++] = ret;
      continue;
    }

    if (ins.op == OP_CHECK_LIST) {
      if (sp < 1) {
        d_fail(result, ErrCode::Value);
        break;
      }
      if (!d_is_typed_list_tag(stack[sp - 1].tag)) {
        d_fail(result, ErrCode::Type);
        break;
      }
      continue;
    }

    if (ins.op == OP_CHECK_INT) {
      if (sp < 1) {
        d_fail(result, ErrCode::Value);
        break;
      }
      if (stack[sp - 1].tag != ValueTag::Int) {
        d_fail(result, ErrCode::Type);
        break;
      }
      continue;
    }

    if (ins.op == OP_EMPTY_LIST) {
      if (!d_has_a(ins) || sp >= StackCapacity) {
        d_fail(result, ErrCode::Value);
        break;
      }
      Value out = Value::invalid();
      if (!d_make_empty_list_for_tag(d_list_tag_from_private_code(ins.a), payload_state, out)) {
        d_fail(result, ErrCode::Type);
        break;
      }
      stack[sp++] = out;
      continue;
    }

    if (ins.op == OP_EMPTY_LIST_LIKE) {
      if (sp < 1) {
        d_fail(result, ErrCode::Value);
        break;
      }
      const Value source = stack[--sp];
      Value out = Value::invalid();
      if (!d_make_empty_list_for_tag(source.tag, payload_state, out)) {
        d_fail(result, ErrCode::Type);
        break;
      }
      stack[sp++] = out;
      continue;
    }

    if constexpr (EnableRegions) {
      if (ins.op == OP_BOUNDED_REGION) {
        if (!d_has_a(ins) || ins.a < 0 || ins.a >= view.region_count) {
          d_fail(result, ErrCode::Value);
          break;
        }
        const int index = view.region_offset + ins.a;
        if (index < 0 || index >= execution_tables.region_segment_count) {
          d_fail(result, ErrCode::Value);
          break;
        }
        const auto& segment = execution_tables.region_segments[index];
        const int count = static_cast<int>(segment.state_count + segment.bound_operand_count);
        if (sp < count) { d_fail(result, ErrCode::Value); break; }
        sp -= count;
        const int saved_strings = payload_state.string_entry_count;
        const int saved_lists = payload_state.list_entry_count;
        const int saved_bytes = payload_state.string_bytes_used;
        const int saved_values = payload_state.list_values_used;
        const DResult out = d_run_bounded_region<Flavor>(
            segment, stack + sp, locals, local_set_mask, payload_tables,
            payload_state, execution_tables, fuel_left, workspace);
        if (out.is_error) { result = out; break; }
        if (!d_is_payload_value(out.value)) {
          payload_state.string_entry_count = saved_strings;
          payload_state.list_entry_count = saved_lists;
          payload_state.string_bytes_used = saved_bytes;
          payload_state.list_values_used = saved_values;
        }
        stack[sp++] = out.value;
        continue;
      }
    }
    if (ins.op == OP_RETURN) {
      if (sp < 1) {
        d_fail(result, ErrCode::Value);
        break;
      }
      result.is_error = 0;
      result.value = stack[sp - 1];
      returned = true;
      break;
    }

    d_fail(result, ErrCode::Type);
    break;
  }

  if (!returned && !result.is_error) {
    if (require_return || sp < 1) {
      d_fail(result, ErrCode::Value);
    } else {
      result = d_ok(stack[sp - 1]);
    }
  }
  return result;
}

template <DPayloadFlavor Flavor, bool EnableRegions = false>
__device__ __noinline__ DResult d_execute_bytecode_impl(const DProgramMeta& meta,
                                                        const DInstr* shared_code,
                                                        const Value* all_consts,
                                                        const Value* shared_case_local_vals,
                                                        const unsigned char* shared_case_local_set,
                                                        const DPayloadTables& payload_tables,
                                                        const DExecutionTables& execution_tables,
                                                        int local_case,
                                                        int fuel,
                                                        DRegionWorkspace workspace = {}) {
  DResult result;
  result.is_error = 0;
  result.err_code = ErrCode::Value;
  result.value = Value::invalid();

  if (!meta.is_valid) {
    d_fail(result, meta.err_code);
    return result;
  }

  DPayloadStateStorage<Flavor> payload_state_storage;
  int fuel_left = fuel;
  const DCodeView view{
      shared_code,
      meta.code_len,
      all_consts + meta.const_offset,
      meta.const_len,
      meta.n_locals,
      meta.region_offset,
      meta.region_count,
  };
  return d_run_code_core<Flavor, EnableRegions>(view, shared_case_local_vals, shared_case_local_set,
                                            local_case, nullptr, 0, payload_tables,
                                            payload_state_storage.ref(), execution_tables, fuel_left, true, workspace);
}

// The host proves this root has exactly operand loads, one region invocation and
// RETURN. Input cases remain immutable global storage; no 64-slot local copy or
// general root operand stack is needed. All instruction charges remain ordered.
__device__ __noinline__ DResult d_execute_region_root(const DProgramMeta& meta,
    const DInstr* code, const Value* constants, const Value* case_values,
    const unsigned char* case_set, const DPayloadTables& payload,
    const DExecutionTables& execution, int local_case, int fuel,
    DRegionWorkspace workspace) {
  if (!meta.is_valid) return d_error(meta.err_code);
  if (meta.region_count != 1 || meta.region_offset < 0 ||
      meta.region_offset >= execution.region_segment_count) return d_error(ErrCode::Value);
  const auto& segment = execution.region_segments[meta.region_offset];
  const auto count = segment.state_count + segment.bound_operand_count;
  if (count > DMAX_REGION_STATES + DMAX_REGION_BOUND_OPERANDS || meta.code_len < count + 2)
    return d_error(ErrCode::Value);
  Value operands[DMAX_REGION_STATES + DMAX_REGION_BOUND_OPERANDS];
  const auto* inputs = case_values + static_cast<std::size_t>(local_case) * MAX_LOCALS;
  const auto* present = case_set + static_cast<std::size_t>(local_case) * MAX_LOCALS;
  std::uint64_t mask = 0;
  for (int local = 0; local < meta.n_locals; ++local) if (present[local]) mask |= std::uint64_t{1} << local;
  unsigned sp = 0;
  const auto invocation = meta.code_len - 2;
  for (int i = 0; i < invocation; ++i) {
    const auto ins = code[i];
    if (fuel < 0 || ins.fuel > static_cast<unsigned>(fuel)) return d_error(ErrCode::Timeout);
    fuel -= static_cast<int>(ins.fuel);
    if (ins.op == OP_CALL_BUILTIN || ins.op == OP_CHECK_LIST || ins.op == OP_CHECK_INT) {
      if (sp == 0) return d_error(ErrCode::Value);
      if (ins.op == OP_CHECK_INT) {
        if (operands[sp - 1].tag != ValueTag::Int) return d_error(ErrCode::Type);
      } else {
        if (operands[sp - 1].tag != ValueTag::IntList) return d_error(ErrCode::Type);
        if (ins.op == OP_CALL_BUILTIN)
          operands[sp - 1] = Value::from_int(Value::container_len(operands[sp - 1]));
      }
      continue;
    }
    if (sp >= DMAX_REGION_STATES + DMAX_REGION_BOUND_OPERANDS) return d_error(ErrCode::Value);
    Value value;
    if (ins.op == OP_LOAD) {
      if (ins.a < 0 || ins.a >= meta.n_locals || !present[ins.a]) return d_error(ErrCode::Name);
      value = inputs[ins.a];
    } else {
      if (ins.op != OP_PUSH_CONST || ins.a < 0 || ins.a >= meta.const_len) return d_error(ErrCode::Value);
      value = constants[meta.const_offset + ins.a];
    }
    if (!d_convert_list_view(value, payload)) return d_error(ErrCode::Value);
    operands[sp++] = value;
  }
  if (sp != count) return d_error(ErrCode::Value);
  if (fuel < 0 || code[invocation].fuel > static_cast<unsigned>(fuel)) return d_error(ErrCode::Timeout);
  fuel -= static_cast<int>(code[invocation].fuel);
  DNoPayloadState state{};
  auto result = d_run_bounded_region<DPayloadFlavor::BoundIntListViews>(segment,
      operands, inputs, mask, payload, state, execution, fuel, workspace);
  if (result.is_error) return result;
  if (fuel < 0 || code[invocation + 1].fuel > static_cast<unsigned>(fuel)) return d_error(ErrCode::Timeout);
  return result;
}

}  // namespace gagp::gpu_detail
