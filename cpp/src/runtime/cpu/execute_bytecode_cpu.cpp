#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/cpu/execution_session.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gagp/core/builtin.hpp"
#include "gagp/core/bytecode_verify.hpp"
#include "region_adapter.hpp"
#include "gagp/core/opcode.hpp"
#include "gagp/core/value_semantics.hpp"
#include "gagp/runtime/cpu/builtins_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"
#include "value_stack.hpp"

namespace gagp {

namespace {

struct LocalSlot {
  bool is_set = false;
  Value value = Value::invalid();
  ValueTag expected = ValueTag::Invalid;
};

ExecResult fail(ErrCode code, const std::string& message) {
  ExecResult out;
  out.is_error = true;
  out.err = Err{code, message};
  return out;
}

bool to_numeric_pair(const Value& a, const Value& b, double& a_out, double& b_out,
                     bool& any_float) {
  return vm_semantics::to_numeric_pair(a, b, a_out, b_out, any_float);
}

bool value_to_bool(const Value& v, bool& out) {
  if (v.tag != ValueTag::Bool) {
    return false;
  }
  out = v.b;
  return true;
}

bool is_list_value(const Value& v) {
  return v.tag == ValueTag::IntList || v.tag == ValueTag::FloatList || v.tag == ValueTag::StringList;
}

Value make_empty_list_for_tag(int tag) {
  const std::vector<Value> elems;
  if (tag == 1) return payload::make_int_list_value(elems);
  if (tag == 2) return payload::make_float_list_value(elems);
  if (tag == 3) return payload::make_string_list_value(elems);
  return Value::invalid();
}

Value make_empty_list_like(const Value& source) {
  const std::vector<Value> elems;
  if (source.tag == ValueTag::IntList) return payload::make_int_list_value(elems);
  if (source.tag == ValueTag::FloatList) return payload::make_float_list_value(elems);
  if (source.tag == ValueTag::StringList) return payload::make_string_list_value(elems);
  return Value::invalid();
}

ExecResult compare_values(const Opcode op, const Value& a, const Value& b) {
  vm_semantics::CmpOp cmp_op = vm_semantics::CmpOp::EQ;
  if (op == Opcode::Lt) cmp_op = vm_semantics::CmpOp::LT;
  else if (op == Opcode::Le) cmp_op = vm_semantics::CmpOp::LE;
  else if (op == Opcode::Gt) cmp_op = vm_semantics::CmpOp::GT;
  else if (op == Opcode::Ge) cmp_op = vm_semantics::CmpOp::GE;
  else if (op == Opcode::Eq) cmp_op = vm_semantics::CmpOp::EQ;
  else if (op == Opcode::Ne) cmp_op = vm_semantics::CmpOp::NE;
  else return fail(ErrCode::Type, "unknown comparison op");

  bool out_bool = false;
  const vm_semantics::CompareStatus status = vm_semantics::compare_values(cmp_op, a, b, out_bool);
  if (status == vm_semantics::CompareStatus::Ok) {
    return ExecResult{false, Value::from_bool(out_bool), Err{ErrCode::Value, ""}};
  }
  if (status == vm_semantics::CompareStatus::BoolOrderingNotSupported) {
    return fail(ErrCode::Type, "ordering comparison on bool not supported");
  }
  if (status == vm_semantics::CompareStatus::InvalidOrderingNotSupported) {
    return fail(ErrCode::Type, "comparison on invalid value not supported");
  }
  if (status == vm_semantics::CompareStatus::UnsupportedTypes) {
    return fail(ErrCode::Type, "unsupported comparison operand types");
  }
  return fail(ErrCode::Type, "unknown comparison op");
}

struct RegionInvocationBuffers {
  std::vector<Value> operands;
  std::vector<CoordinateDomain> domains;
  std::vector<std::optional<Value>> parameters;
  detail::RegionPhaseScratch phases;
};

struct RegionRunContext {
  struct Validation {
    int caller_n_locals = -1;
    std::optional<BytecodeVerifyResult> result;
  };
  std::vector<Validation> validations;
  detail::RegionScratch scratch;
  RegionInvocationBuffers buffers;

  const BytecodeVerifyResult& validate(std::size_t index,
                                      const BoundedRegionSegment& segment,
                                      int caller_n_locals) {
    auto& entry = validations.at(index);
    if (!entry.result || entry.caller_n_locals != caller_n_locals) {
      entry.result = verify_bounded_region_segment(segment, caller_n_locals);
      entry.caller_n_locals = caller_n_locals;
    }
    return *entry.result;
  }
};

struct CodeView {
  const std::vector<Value>& consts;
  const std::vector<Instr>& code;
  int n_locals = 0;
  const std::unordered_map<std::string, int>& var2idx;
  const std::vector<std::uint32_t>& instruction_fuel;
  const std::vector<std::pair<int, ValueTag>>* preset_types = nullptr;
  RegionRunContext* region_context = nullptr;
};

bool is_asgp_dc_source(const Value& v) {
  return v.tag == ValueTag::String || v.tag == ValueTag::IntList || v.tag == ValueTag::FloatList ||
         v.tag == ValueTag::StringList;
}

ExecResult run_code(const CodeView& view,
                    const std::vector<std::pair<int, Value>>& inputs,
                    const std::vector<std::pair<int, Value>>& preset_locals,
                    int& fuel,
                    bool require_return,
                    const BytecodeProgram* root_program);

ExecResult run_phase(const PhaseProgram& phase,
                     const std::vector<std::pair<int, Value>>& bindings,
                     int& fuel,
                     const BytecodeProgram* root_program) {
  std::vector<std::pair<int, Value>> preset_locals;
  preset_locals.reserve(bindings.size());
  for (const auto& item : bindings) {
    auto it = phase.binder_locals.find(item.first);
    if (it != phase.binder_locals.end()) {
      preset_locals.push_back({it->second, item.second});
    }
  }
  return run_code(CodeView{phase.consts, phase.code, phase.n_locals, phase.var2idx, phase.instruction_fuel}, {}, preset_locals, fuel, false,
                  root_program);
}

ExecResult asgp_dc_slice(const Value& source, int start, int end) {
  BuiltinResult out = builtin_call(BuiltinId::Slice, {source, Value::from_int(start), Value::from_int(end)});
  if (out.is_error) {
    return ExecResult{true, Value::invalid(), out.err};
  }
  return ExecResult{false, out.value, Err{ErrCode::Value, ""}};
}

ExecResult eval_asgp_dc(const AsgpDcSegment& segment,
                        const Value& source,
                        long long lo,
                        int& fuel,
                        const BytecodeProgram* root_program) {
  if (fuel <= 0) {
    return fail(ErrCode::Timeout, "out of fuel");
  }
  fuel -= 1;
  if (!is_asgp_dc_source(source)) {
    return fail(ErrCode::Type, "ASGP-DC source must be String, IntList, FloatList, or StringList");
  }
  const int n = static_cast<int>(Value::container_len(source));
  if (n <= 1) {
    return run_phase(
        segment.solve,
        {
            {segment.solve_xs_name, source},
            {segment.solve_n_name, Value::from_int(n)},
            {segment.solve_lo_name, Value::from_int(lo)},
        },
        fuel, root_program);
  }

  ExecResult raw_split = run_phase(segment.divide, {{segment.divide_n_name, Value::from_int(n)}}, fuel, root_program);
  if (raw_split.is_error) {
    return raw_split;
  }
  if (raw_split.value.tag != ValueTag::Int) {
    return fail(ErrCode::Type, "ASGP-DC divide phase must return int");
  }
  long long split_ll = raw_split.value.i;
  if (split_ll < 1) split_ll = 1;
  if (split_ll > n - 1) split_ll = n - 1;
  const int split = static_cast<int>(split_ll);

  ExecResult left_source = asgp_dc_slice(source, 0, split);
  if (left_source.is_error) return left_source;
  ExecResult left = eval_asgp_dc(segment, left_source.value, lo, fuel, root_program);
  if (left.is_error) return left;

  ExecResult right_source = asgp_dc_slice(source, split, n);
  if (right_source.is_error) return right_source;
  ExecResult right = eval_asgp_dc(segment, right_source.value, lo + split, fuel, root_program);
  if (right.is_error) return right;

  if (left.value.tag != right.value.tag) {
    return fail(ErrCode::Type, "ASGP-DC recursive results must have matching types");
  }
  ExecResult out = run_phase(
      segment.combine,
      {
          {segment.combine_left_name, left.value},
          {segment.combine_right_name, right.value},
      },
      fuel, root_program);
  if (out.is_error) return out;
  if (out.value.tag != left.value.tag) {
    return fail(ErrCode::Type, "ASGP-DC combine result type must match recursive result type");
  }
  return out;
}

std::vector<long long> asgp_dp1d_deps(const AsgpDp1dSegment& segment, long long state) {
  const bool backward = segment.dep_kind < 0;
  std::vector<long long> out;
  out.reserve(segment.dep_offsets.size());
  for (const int offset : segment.dep_offsets) {
    out.push_back(backward ? (state - offset) : (state + offset));
  }
  return out;
}

std::uint64_t asgp_dp2d_key(long long i, long long j) {
  const auto i32 = static_cast<std::uint32_t>(i);
  const auto j32 = static_cast<std::uint32_t>(j);
  return (static_cast<std::uint64_t>(i32) << 32) | static_cast<std::uint64_t>(j32);
}

std::vector<std::pair<long long, long long>> asgp_dp2d_deps(const AsgpDp2dSegment& segment,
                                                           long long i,
                                                           long long j) {
  switch (segment.dep_kind) {
    case 0:
      return {{i - 1, j}, {i, j - 1}};
    case 1:
      return {{i + 1, j}, {i, j + 1}};
    case 2:
      return {{i - 1, j - 1}};
    case 3:
      return {{i + 1, j + 1}};
    case 4:
      return {{i - 1, j}, {i, j - 1}, {i - 1, j - 1}};
    case 5:
      return {{i + 1, j}, {i, j + 1}, {i + 1, j + 1}};
    default:
      return {};
  }
}

ExecResult eval_asgp_dp1d(const AsgpDp1dSegment& segment,
                          long long state,
                          std::unordered_map<long long, Value>& memo,
                          int& fuel,
                          const BytecodeProgram* root_program) {
  if (fuel <= 0) {
    return fail(ErrCode::Timeout, "out of fuel");
  }
  fuel -= 1;

  if (state < segment.lo || state > segment.hi) {
    return ExecResult{false, segment.boundary_value, Err{ErrCode::Value, ""}};
  }
  if (state == segment.base_state) {
    return run_phase(segment.solve, {{segment.solve_state_name, Value::from_int(state)}}, fuel, root_program);
  }
  auto cached = memo.find(state);
  if (cached != memo.end()) {
    return ExecResult{false, cached->second, Err{ErrCode::Value, ""}};
  }

  std::vector<Value> dep_values;
  for (const long long dep_state : asgp_dp1d_deps(segment, state)) {
    ExecResult dep = eval_asgp_dp1d(segment, dep_state, memo, fuel, root_program);
    if (dep.is_error) return dep;
    dep_values.push_back(dep.value);
  }
  for (std::size_t i = 1; i < dep_values.size(); ++i) {
    if (dep_values[i].tag != dep_values[0].tag) {
      return fail(ErrCode::Type, "ASGP-DP1D dependency result types must match");
    }
  }

  std::vector<std::pair<int, Value>> bindings;
  bindings.reserve(1 + dep_values.size());
  bindings.push_back({segment.transition_state_name, Value::from_int(state)});
  for (std::size_t i = 0; i < dep_values.size() && i < segment.transition_dep_names.size(); ++i) {
    bindings.push_back({segment.transition_dep_names[i], dep_values[i]});
  }
  ExecResult out = run_phase(segment.transition, bindings, fuel, root_program);
  if (out.is_error) return out;
  if (!dep_values.empty() && out.value.tag != dep_values[0].tag) {
    return fail(ErrCode::Type, "ASGP-DP1D transition result type must match dependency result type");
  }
  memo[state] = out.value;
  return out;
}

ExecResult eval_asgp_dp2d(const AsgpDp2dSegment& segment,
                          long long i,
                          long long j,
                          std::unordered_map<std::uint64_t, Value>& memo,
                          int& fuel,
                          const BytecodeProgram* root_program) {
  if (fuel <= 0) {
    return fail(ErrCode::Timeout, "out of fuel");
  }
  fuel -= 1;

  if (i < segment.i_lo || i > segment.i_hi || j < segment.j_lo || j > segment.j_hi) {
    return ExecResult{false, segment.boundary_value, Err{ErrCode::Value, ""}};
  }
  if (i == segment.base_i && j == segment.base_j) {
    return run_phase(
        segment.solve,
        {
            {segment.solve_i_name, Value::from_int(i)},
            {segment.solve_j_name, Value::from_int(j)},
        },
        fuel, root_program);
  }

  const std::uint64_t key = asgp_dp2d_key(i, j);
  auto cached = memo.find(key);
  if (cached != memo.end()) {
    return ExecResult{false, cached->second, Err{ErrCode::Value, ""}};
  }

  const std::vector<std::pair<long long, long long>> dep_cells = asgp_dp2d_deps(segment, i, j);
  std::vector<Value> dep_values;
  dep_values.reserve(dep_cells.size());
  for (const auto& dep_cell : dep_cells) {
    ExecResult dep = eval_asgp_dp2d(segment, dep_cell.first, dep_cell.second, memo, fuel, root_program);
    if (dep.is_error) return dep;
    dep_values.push_back(dep.value);
  }
  for (std::size_t dep_idx = 1; dep_idx < dep_values.size(); ++dep_idx) {
    if (dep_values[dep_idx].tag != dep_values[0].tag) {
      return fail(ErrCode::Type, "ASGP-DP2D dependency result types must match");
    }
  }

  std::vector<std::pair<int, Value>> bindings;
  bindings.reserve(2 + dep_values.size());
  bindings.push_back({segment.transition_i_name, Value::from_int(i)});
  bindings.push_back({segment.transition_j_name, Value::from_int(j)});
  for (std::size_t dep_idx = 0; dep_idx < dep_values.size() && dep_idx < segment.transition_dep_names.size();
       ++dep_idx) {
    bindings.push_back({segment.transition_dep_names[dep_idx], dep_values[dep_idx]});
  }
  ExecResult out = run_phase(segment.transition, bindings, fuel, root_program);
  if (out.is_error) return out;
  if (!dep_values.empty() && out.value.tag != dep_values[0].tag) {
    return fail(ErrCode::Type, "ASGP-DP2D transition result type must match dependency result type");
  }
  memo[key] = out.value;
  return out;
}

template <bool SemanticFuel>
ExecResult run_code_impl(const CodeView& view,
                    const std::vector<std::pair<int, Value>>& inputs,
                    const std::vector<std::pair<int, Value>>& preset_locals,
                    int& fuel,
                    bool require_return,
                    const BytecodeProgram* root_program) {
  detail::ValueStack stack;
  std::array<LocalSlot, 16> inline_locals;
  std::vector<LocalSlot> heap_locals;
  LocalSlot* locals = inline_locals.data();
  if (static_cast<std::size_t>(view.n_locals) > inline_locals.size()) {
    heap_locals.resize(static_cast<std::size_t>(view.n_locals));
    locals = heap_locals.data();
  }

  for (const auto& item : inputs) {
    const int idx = item.first;
    if (idx >= 0 && idx < view.n_locals) {
      locals[static_cast<std::size_t>(idx)].is_set = true;
      locals[static_cast<std::size_t>(idx)].value = item.second;
    }
  }
  for (const auto& item : preset_locals) {
    const int idx = item.first;
    if (idx < 0 || idx >= view.n_locals) {
      return fail(ErrCode::Name, "local index out of range");
    }
    locals[static_cast<std::size_t>(idx)].is_set = true;
    locals[static_cast<std::size_t>(idx)].value = item.second;
  }

  if (view.preset_types)
    for (const auto& item : *view.preset_types) locals[item.first].expected = item.second;

  if constexpr (SemanticFuel) {
    if (view.instruction_fuel.size() != view.code.size())
      return fail(ErrCode::Value, "instruction fuel schedule size mismatch");
  }
  std::size_t uncharged_steps = 0;
  int ip = 0;
  while (ip < static_cast<int>(view.code.size())) {
    if constexpr (SemanticFuel) {
      const auto cost = view.instruction_fuel[static_cast<std::size_t>(ip)];
      if (cost > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
        return fail(ErrCode::Value, "instruction fuel cost exceeds signed fuel capacity");
      if (fuel < static_cast<int>(cost))
        return fail(ErrCode::Timeout, "out of fuel");
      fuel -= static_cast<int>(cost);
      if (cost != 0) {
        uncharged_steps = 0;
      } else if (++uncharged_steps > view.code.size()) {
        return fail(ErrCode::Value, "zero-cost instruction cycle");
      }
    } else {
      if (fuel <= 0) return fail(ErrCode::Timeout, "out of fuel");
      fuel -= 1;
    }

    const Instr& ins = view.code[static_cast<std::size_t>(ip)];
    ip += 1;
    const Opcode op = ins.op;

    switch (op) {
      case Opcode::PushConst: {
        if (!ins.has_a || ins.a < 0 || ins.a >= static_cast<int>(view.consts.size())) {
          return fail(ErrCode::Value, "const index out of range");
        }
        stack.push_back(view.consts[static_cast<std::size_t>(ins.a)]);
        continue;
      }

      case Opcode::Load: {
        if (!ins.has_a || ins.a < 0 || ins.a >= view.n_locals) {
          return fail(ErrCode::Name, "local index out of range");
        }
        const LocalSlot& slot = locals[static_cast<std::size_t>(ins.a)];
        if (!slot.is_set) {
          return fail(ErrCode::Name, "read of uninitialized local");
        }
        if (slot.expected != ValueTag::Invalid && slot.value.tag != slot.expected)
          return fail(ErrCode::Type, "bounded region binding type mismatch");
        stack.push_back(slot.value);
        continue;
      }

      case Opcode::Store: {
        if (!ins.has_a || ins.a < 0 || ins.a >= view.n_locals) {
          return fail(ErrCode::Name, "local index out of range");
        }
        if (stack.empty()) {
          return fail(ErrCode::Value, "stack underflow");
        }
        locals[static_cast<std::size_t>(ins.a)].is_set = true;
        locals[static_cast<std::size_t>(ins.a)].value = stack.back();
        locals[static_cast<std::size_t>(ins.a)].expected = ValueTag::Invalid;
        stack.pop_back();
        continue;
      }

      case Opcode::CheckList: {
        if (stack.empty()) {
          return fail(ErrCode::Value, "stack underflow");
        }
        if (!is_list_value(stack.back())) {
          return fail(ErrCode::Type, "structured list source must be a typed list");
        }
        continue;
      }

      case Opcode::CheckInt: {
        if (stack.empty()) {
          return fail(ErrCode::Value, "stack underflow");
        }
        if (stack.back().tag != ValueTag::Int) {
          return fail(ErrCode::Type, "expected int");
        }
        continue;
      }

      case Opcode::EmptyList: {
        if (!ins.has_a) {
          return fail(ErrCode::Type, "EMPTY_LIST requires list tag");
        }
        const Value out = make_empty_list_for_tag(ins.a);
        if (out.tag == ValueTag::Invalid) {
          return fail(ErrCode::Type, "unknown list tag");
        }
        stack.push_back(out);
        continue;
      }

      case Opcode::EmptyListLike: {
        if (stack.empty()) {
          return fail(ErrCode::Value, "stack underflow");
        }
        const Value source = stack.back();
        stack.pop_back();
        if (!is_list_value(source)) {
          return fail(ErrCode::Type, "EMPTY_LIST_LIKE expects typed list");
        }
        stack.push_back(make_empty_list_like(source));
        continue;
      }

      case Opcode::Neg:
      case Opcode::Not: {
        if (stack.empty()) {
          return fail(ErrCode::Value, "stack underflow");
        }
        const Value x = stack.back();
        stack.pop_back();
        if (op == Opcode::Neg) {
          if (!is_numeric(x)) {
            return fail(ErrCode::Type, "NEG expects numeric");
          }
          if (x.tag == ValueTag::Float) {
            stack.push_back(Value::from_float(vm_semantics::canonicalize_vm_float(-x.f)));
          } else {
            stack.push_back(Value::from_int(vm_semantics::wrap_int_neg(x.i)));
          }
        } else {
          if (x.tag != ValueTag::Bool) {
            return fail(ErrCode::Type, "NOT expects bool");
          }
          stack.push_back(Value::from_bool(!x.b));
        }
        continue;
      }

      case Opcode::Add:
      case Opcode::Sub:
      case Opcode::Mul:
      case Opcode::Div:
      case Opcode::Mod: {
        if (stack.size() < 2) {
          return fail(ErrCode::Value, "stack underflow");
        }
        const Value b = stack.back();
        stack.pop_back();
        const Value a = stack.back();
        stack.pop_back();

        double a_num = 0.0;
        double b_num = 0.0;
        bool any_float = false;
        if (!to_numeric_pair(a, b, a_num, b_num, any_float)) {
          return fail(ErrCode::Type, std::string(opcode_name(op)) + " expects numeric operands");
        }

        if ((op == Opcode::Div || op == Opcode::Mod) && b_num == 0.0) {
          return fail(ErrCode::ZeroDiv, (op == Opcode::Div) ? "division by zero" : "modulo by zero");
        }

        if (op == Opcode::Add) {
          if (any_float) {
            stack.push_back(Value::from_float(vm_semantics::canonicalize_vm_float(a_num + b_num)));
          } else {
            stack.push_back(Value::from_int(
                vm_semantics::wrap_int_add(static_cast<long long>(a_num), static_cast<long long>(b_num))));
          }
        } else if (op == Opcode::Sub) {
          if (any_float) {
            stack.push_back(Value::from_float(vm_semantics::canonicalize_vm_float(a_num - b_num)));
          } else {
            stack.push_back(Value::from_int(
                vm_semantics::wrap_int_sub(static_cast<long long>(a_num), static_cast<long long>(b_num))));
          }
        } else if (op == Opcode::Mul) {
          if (any_float) {
            stack.push_back(Value::from_float(vm_semantics::canonicalize_vm_float(a_num * b_num)));
          } else {
            stack.push_back(Value::from_int(
                vm_semantics::wrap_int_mul(static_cast<long long>(a_num), static_cast<long long>(b_num))));
          }
        } else if (op == Opcode::Div) {
          stack.push_back(Value::from_float(vm_semantics::canonicalize_vm_float(a_num / b_num)));
        } else if (any_float) {
          stack.push_back(
              Value::from_float(vm_semantics::canonicalize_vm_float(vm_semantics::py_float_mod(a_num, b_num))));
        } else {
          const long long ai = static_cast<long long>(a_num);
          const long long bi = static_cast<long long>(b_num);
          stack.push_back(Value::from_int(vm_semantics::py_int_mod(ai, bi)));
        }
        continue;
      }

      case Opcode::Lt:
      case Opcode::Le:
      case Opcode::Gt:
      case Opcode::Ge:
      case Opcode::Eq:
      case Opcode::Ne: {
        if (stack.size() < 2) {
          return fail(ErrCode::Value, "stack underflow");
        }
        const Value b = stack.back();
        stack.pop_back();
        const Value a = stack.back();
        stack.pop_back();
        ExecResult cmp = compare_values(op, a, b);
        if (cmp.is_error) {
          return cmp;
        }
        stack.push_back(cmp.value);
        continue;
      }

      case Opcode::Jmp: {
        if (!ins.has_a || ins.a < 0 || ins.a > static_cast<int>(view.code.size())) {
          return fail(ErrCode::Value, "jump target out of range");
        }
        ip = ins.a;
        continue;
      }

      case Opcode::JmpIfFalse:
      case Opcode::JmpIfTrue: {
        if (stack.empty()) {
          return fail(ErrCode::Value, "stack underflow");
        }
        if (!ins.has_a || ins.a < 0 || ins.a > static_cast<int>(view.code.size())) {
          return fail(ErrCode::Value, "jump target out of range");
        }
        const Value c = stack.back();
        stack.pop_back();
        bool cond = false;
        if (!value_to_bool(c, cond)) {
          return fail(ErrCode::Type, "jump condition must be bool");
        }
        if (op == Opcode::JmpIfFalse && !cond) ip = ins.a;
        if (op == Opcode::JmpIfTrue && cond) ip = ins.a;
        continue;
      }

      case Opcode::CallBuiltin: {
        const int bid = ins.has_a ? ins.a : -1;
        const int argc = ins.has_b ? ins.b : -1;
        if (argc < 0) {
          return fail(ErrCode::Type, "invalid builtin argc");
        }
        if (static_cast<int>(stack.size()) < argc) {
          return fail(ErrCode::Value, "stack underflow");
        }
        const std::size_t start = stack.size() - static_cast<std::size_t>(argc);

        BuiltinId builtin_id = BuiltinId::Abs;
        if (!builtin_id_from_int(bid, builtin_id)) {
          return fail(ErrCode::Name, "unknown builtin id");
        }

        // Builtins borrow operands for this call and cannot mutate the VM stack.
        // Keep the values alive until the call returns, then consume the range.
        const Value* args = argc == 0 ? nullptr : stack.data() + start;
        BuiltinResult out = builtin_call(builtin_id, args, static_cast<std::size_t>(argc));
        stack.truncate(start);
        if (out.is_error) {
          return ExecResult{true, Value::invalid(), out.err};
        }
        stack.push_back(out.value);
        continue;
      }

      case Opcode::AsgpDc: {
        if (root_program == nullptr || !ins.has_a || ins.a < 0 ||
            ins.a >= static_cast<int>(root_program->asgp_dc_segments.size())) {
          return fail(ErrCode::Value, "ASGP-DC segment index out of range");
        }
        if (stack.empty()) {
          return fail(ErrCode::Value, "stack underflow");
        }
        const Value source = stack.back();
        stack.pop_back();
        ExecResult out =
            eval_asgp_dc(root_program->asgp_dc_segments[static_cast<std::size_t>(ins.a)], source, 0, fuel,
                         root_program);
        if (out.is_error) {
          return out;
        }
        stack.push_back(out.value);
        continue;
      }

      case Opcode::AsgpDp1d: {
        if (root_program == nullptr || !ins.has_a || ins.a < 0 ||
            ins.a >= static_cast<int>(root_program->asgp_dp1d_segments.size())) {
          return fail(ErrCode::Value, "ASGP-DP1D segment index out of range");
        }
        if (stack.empty()) {
          return fail(ErrCode::Value, "stack underflow");
        }
        const Value state = stack.back();
        stack.pop_back();
        if (state.tag != ValueTag::Int) {
          return fail(ErrCode::Type, "ASGP-DP1D state must be int");
        }
        std::unordered_map<long long, Value> memo;
        ExecResult out =
            eval_asgp_dp1d(root_program->asgp_dp1d_segments[static_cast<std::size_t>(ins.a)], state.i, memo, fuel,
                           root_program);
        if (out.is_error) {
          return out;
        }
        stack.push_back(out.value);
        continue;
      }

      case Opcode::AsgpDp2d: {
        if (root_program == nullptr || !ins.has_a || ins.a < 0 ||
            ins.a >= static_cast<int>(root_program->asgp_dp2d_segments.size())) {
          return fail(ErrCode::Value, "ASGP-DP2D segment index out of range");
        }
        if (stack.size() < 2) {
          return fail(ErrCode::Value, "stack underflow");
        }
        const Value state_j = stack.back();
        stack.pop_back();
        const Value state_i = stack.back();
        stack.pop_back();
        if (state_i.tag != ValueTag::Int) {
          return fail(ErrCode::Type, "ASGP-DP2D state_i must be int");
        }
        if (state_j.tag != ValueTag::Int) {
          return fail(ErrCode::Type, "ASGP-DP2D state_j must be int");
        }
        std::unordered_map<std::uint64_t, Value> memo;
        ExecResult out = eval_asgp_dp2d(root_program->asgp_dp2d_segments[static_cast<std::size_t>(ins.a)],
                                        state_i.i, state_j.i, memo, fuel, root_program);
        if (out.is_error) {
          return out;
        }
        stack.push_back(out.value);
        continue;
      }

      case Opcode::BoundedRegion: {
        if (root_program == nullptr || !ins.has_a || ins.a < 0 ||
            static_cast<std::size_t>(ins.a) >= root_program->bounded_region_segments.size())
          return fail(ErrCode::Value, "bounded region segment index out of range");
        const auto& segment = root_program->bounded_region_segments[ins.a];
        try {
          BytecodeVerifyResult one_shot_validation;
          const BytecodeVerifyResult* verified;
          if (view.region_context) {
            verified = &view.region_context->validate(
                static_cast<std::size_t>(ins.a), segment, view.n_locals);
          } else {
            one_shot_validation = verify_bounded_region_segment(segment, view.n_locals);
            verified = &one_shot_validation;
          }
          if (!verified->ok) return fail(ErrCode::Value, verified->diagnostic.message);
          const auto& plan = segment.plan;
          const auto count = plan.state_types.size() + plan.bound_operand_count;
          if (stack.size() < count) return fail(ErrCode::Value, "stack underflow");
          RegionInvocationBuffers one_shot_buffers;
          auto& buffers = view.region_context ? view.region_context->buffers : one_shot_buffers;
          auto& operands = buffers.operands;
          operands.resize(count);
          for (std::size_t i = count; i > 0; --i) {
            operands[i - 1] = stack.back();
            stack.pop_back();
          }
          for (std::size_t i = plan.state_types.size(); i < count; ++i)
            if (operands[i].tag != ValueTag::Int)
              return fail(ErrCode::Type, "bounded region bound operand must be Int");
          auto& domains = buffers.domains;
          domains.clear();
          if (plan.progress == RegionProgressKind::Coordinates) {
            bool dynamic_bounds = false;
            auto bound = [&](const RegionBound& source, std::int64_t& value) {
              if (source.kind == RegionBoundKind::Literal) { value = source.literal; return true; }
              dynamic_bounds = true;
              if (operands[source.operand].tag != ValueTag::Int) return false;
              value = operands[source.operand].i;
              return true;
            };
            for (const auto& domain : plan.coordinate_domains) {
              CoordinateDomain resolved;
              if (!bound(domain.lower, resolved.lower) || !bound(domain.upper, resolved.upper))
                return fail(ErrCode::Type, "bounded region coordinate bound must be Int");
              domains.push_back(resolved);
            }
            // Literal domain arithmetic was proved by segment validation.
            // Invocation operands still require checking their resolved bounds.
            if (dynamic_bounds) {
              CoordinateRecurrence recurrence;
              recurrence.domains = domains;
              recurrence.endpoint = plan.coordinate_endpoint;
              recurrence.rank = plan.coordinate_rank;
              recurrence.duplicate_policy = plan.duplicate_policy;
              for (const auto& request : plan.requests) {
                std::vector<std::int64_t> offsets;
                for (const auto slot : plan.coordinate_slots) offsets.push_back(request.states[slot].offset);
                recurrence.offsets.push_back(std::move(offsets));
              }
              validate_coordinate_recurrence(recurrence);
            }
          }
          auto& parameters = buffers.parameters;
          parameters.clear();
          for (const auto index : segment.parameter_locals) {
            const auto& local = locals[index];
            parameters.push_back(local.is_set ? std::optional<Value>(local.value) : std::nullopt);
          }
          auto phase_runner = [](const PhaseProgram& phase,
                                 const std::vector<std::pair<int, Value>>& presets,
                                 const std::vector<std::pair<int, ValueTag>>& types, int& remaining) {
            return run_code(CodeView{phase.consts, phase.code, phase.n_locals, phase.var2idx,
                                     phase.instruction_fuel, &types}, {}, presets, remaining, false, nullptr);
          };
          detail::RegionAdapter<decltype(phase_runner)> adapter{
              segment, parameters, domains, phase_runner, buffers.phases};
          detail::RegionState initial{};
          for (std::size_t i = 0; i < plan.state_types.size(); ++i) initial[i] = operands[i];
          detail::RegionExecutionLayout layout;
          layout.state_count = static_cast<std::uint32_t>(plan.state_types.size());
          layout.request_count = static_cast<std::uint32_t>(plan.requests.size());
          layout.frame_limit = plan.limits.frames;
          layout.cell_limit = plan.limits.cells;
          layout.entry_fuel = plan.limits.entry_fuel;
          layout.memoized = plan.memoized;
          detail::RegionScratch one_shot_scratch;
          auto& scratch = view.region_context ? view.region_context->scratch : one_shot_scratch;
          auto result = detail::execute_bounded_region(layout, initial, adapter, scratch, fuel);
          if (result.is_error) return result;
          stack.push_back(result.value);
        } catch (const std::exception& error) {
          return fail(ErrCode::Value, error.what());
        }
        continue;
      }

      case Opcode::Return: {
        if (stack.empty()) {
          return fail(ErrCode::Value, "return requires value on stack");
        }
        ExecResult out;
        out.value = stack.back();
        return out;
      }
      default: {
        return fail(ErrCode::Value, "unknown opcode");
      }
    }
  }

  if (require_return) {
    return fail(ErrCode::Value, "program finished without return");
  }
  if (stack.empty()) {
    return fail(ErrCode::Value, "expression segment produced no value");
  }
  ExecResult out;
  out.value = stack.back();
  return out;
}

ExecResult run_code(const CodeView& view,
    const std::vector<std::pair<int, Value>>& inputs,
    const std::vector<std::pair<int, Value>>& preset_locals,
    int& fuel, bool require_return, const BytecodeProgram* root_program) {
  if (view.instruction_fuel.empty())
    return run_code_impl<false>(view, inputs, preset_locals, fuel, require_return, root_program);
  return run_code_impl<true>(view, inputs, preset_locals, fuel, require_return, root_program);
}

}  // namespace

struct CpuExecutionSession::Impl {
  explicit Impl(BytecodeProgram snapshot) : program(std::move(snapshot)) {
    context.validations.resize(program.bounded_region_segments.size());
  }
  const BytecodeProgram program;
  RegionRunContext context;
};

CpuExecutionSession::CpuExecutionSession(BytecodeProgram program)
    : impl_(std::make_unique<Impl>(std::move(program))) {}
CpuExecutionSession::~CpuExecutionSession() = default;
CpuExecutionSession::CpuExecutionSession(CpuExecutionSession&&) noexcept = default;
CpuExecutionSession& CpuExecutionSession::operator=(CpuExecutionSession&&) noexcept = default;

ExecResult CpuExecutionSession::execute(
    const std::vector<std::pair<int, Value>>& inputs, int fuel) {
  if (!impl_) return fail(ErrCode::Value, "execution session has been moved");
  const auto& program = impl_->program;
  return run_code(CodeView{program.consts, program.code, program.n_locals,
                          program.var2idx, program.instruction_fuel, nullptr,
                          &impl_->context}, inputs, {}, fuel, true, &program);
}

std::size_t CpuExecutionSession::retained_region_bytes() const noexcept {
  return impl_ ? impl_->context.scratch.storage_bytes() : 0;
}

ExecResult execute_bytecode_cpu(const BytecodeProgram& program,
                                const std::vector<std::pair<int, Value>>& inputs,
                                int fuel) {
  return run_code(CodeView{program.consts, program.code, program.n_locals, program.var2idx, program.instruction_fuel}, inputs, {}, fuel, true,
                  &program);
}

}  // namespace gagp
