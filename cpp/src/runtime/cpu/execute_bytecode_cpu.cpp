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
#include "list_views.hpp"
#include <cstdlib>
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
    bool views_supported = false;
  };
  std::vector<Validation> validations;
  detail::RegionScratch scratch;
  RegionInvocationBuffers buffers;
  std::unique_ptr<detail::CpuListViews> list_views;

  const BytecodeVerifyResult& validate(std::size_t index,
                                      const BoundedRegionSegment& segment,
                                      int caller_n_locals) {
    auto& entry = validations.at(index);
    if (!entry.result || entry.caller_n_locals != caller_n_locals) {
      entry.result = verify_bounded_region_segment(segment, caller_n_locals);
      entry.caller_n_locals = caller_n_locals;
      entry.views_supported = entry.result->ok && std::getenv("GAGP_CPU_REGION_VIEWS") &&
          detail::cpu_region_views_supported(segment);
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

ExecResult run_code(const CodeView& view,
                    const std::vector<std::pair<int, Value>>& inputs,
                    const std::vector<std::pair<int, Value>>& preset_locals,
                    int& fuel,
                    bool require_return,
                    const BytecodeProgram* root_program);

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
          detail::RegionState initial{};
          for (std::size_t i = 0; i < plan.state_types.size(); ++i) initial[i] = operands[i];
          auto phase_runner = [](const PhaseProgram& phase,
                                 const std::vector<std::pair<int, Value>>& presets,
                                 const std::vector<std::pair<int, ValueTag>>& types, int& remaining) {
            return run_code(CodeView{phase.consts, phase.code, phase.n_locals, phase.var2idx,
                                     phase.instruction_fuel, &types}, {}, presets, remaining, false, nullptr);
          };
          detail::RegionAdapter<decltype(phase_runner)> adapter{
              segment, parameters, domains, phase_runner, buffers.phases};
          detail::RegionExecutionLayout layout;
          layout.state_count = static_cast<std::uint32_t>(plan.state_types.size());
          layout.request_count = static_cast<std::uint32_t>(plan.requests.size());
          layout.frame_limit = plan.limits.frames;
          layout.cell_limit = plan.limits.cells;
          layout.entry_fuel = plan.limits.entry_fuel;
          layout.memoized = plan.memoized;
          detail::RegionScratch one_shot_scratch;
          auto& scratch = view.region_context ? view.region_context->scratch : one_shot_scratch;
          const bool use_views = view.region_context ? view.region_context->validations[ins.a].views_supported :
              std::getenv("GAGP_CPU_REGION_VIEWS") && detail::cpu_region_views_supported(segment);
          auto result = use_views
              ? detail::execute_with_cpu_views(layout, initial, adapter, scratch, fuel,
                    parameters, view.region_context ? &view.region_context->list_views : nullptr)
              : detail::execute_bounded_region(layout, initial, adapter, scratch, fuel);
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
