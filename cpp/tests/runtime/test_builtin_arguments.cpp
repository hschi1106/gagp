#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "gagp/core/builtin.hpp"
#include "gagp/core/bytecode.hpp"
#include "gagp/core/errors.hpp"
#include "gagp/core/value.hpp"
#include "gagp/runtime/cpu/builtins_cpu.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace {
using gagp::BuiltinId;
using gagp::BuiltinResult;
using gagp::BytecodeProgram;
using gagp::ErrCode;
using gagp::ExecResult;
using gagp::Instr;
using gagp::Opcode;
using gagp::Value;
using gagp::ValueTag;

Instr ins(Opcode op) { return {op, 0, 0, false, false}; }
Instr ins_a(Opcode op, int a) { return {op, a, 0, true, false}; }
Instr ins_ab(Opcode op, int a, int b) { return {op, a, b, true, true}; }

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << '\n';
  return condition;
}

bool is_error(const ExecResult& result, ErrCode code) {
  return result.is_error && result.err.code == code;
}

bool exact_value(const Value& lhs, const Value& rhs) {
  if (lhs.tag != rhs.tag) return false;
  switch (lhs.tag) {
    case ValueTag::Int:
    case ValueTag::Char:
    case ValueTag::FallbackToken:
      return lhs.i == rhs.i;
    case ValueTag::Float:
      return lhs.f == rhs.f;
    case ValueTag::Bool:
      return lhs.b == rhs.b;
    case ValueTag::String: {
      std::string a;
      std::string b;
      const bool have_a = gagp::payload::lookup_string(lhs, &a);
      const bool have_b = gagp::payload::lookup_string(rhs, &b);
      return have_a == have_b && (have_a ? a == b : lhs.i == rhs.i);
    }
    case ValueTag::IntList:
    case ValueTag::FloatList:
    case ValueTag::StringList: {
      std::vector<Value> a;
      std::vector<Value> b;
      const bool have_a = gagp::payload::lookup_list(lhs, &a);
      const bool have_b = gagp::payload::lookup_list(rhs, &b);
      if (have_a != have_b) return false;
      if (!have_a) return lhs.i == rhs.i;
      if (a.size() != b.size()) return false;
      for (std::size_t i = 0; i < a.size(); ++i) {
        if (!exact_value(a[i], b[i])) return false;
      }
      return true;
    }
    case ValueTag::Invalid:
      return true;
  }
  return false;
}

bool same_builtin_result(const BuiltinResult& lhs, const BuiltinResult& rhs) {
  if (lhs.is_error != rhs.is_error) return false;
  return lhs.is_error ? lhs.err.code == rhs.err.code
                      : exact_value(lhs.value, rhs.value);
}

BytecodeProgram call_program(int builtin, int argc,
                             std::vector<Value> operands = {}) {
  BytecodeProgram result;
  result.consts = std::move(operands);
  for (std::size_t i = 0; i < result.consts.size(); ++i) {
    result.code.push_back(ins_a(Opcode::PushConst, static_cast<int>(i)));
  }
  result.code.push_back(ins_ab(Opcode::CallBuiltin, builtin, argc));
  return result;
}

bool exact_error_boundary(const BytecodeProgram& program, int boundary,
                          ErrCode code, const std::string& label) {
  return check(is_error(gagp::execute_bytecode_cpu(program, {}, boundary - 1),
                        ErrCode::Timeout),
               label + ": operation ran before its fuel boundary") &&
         check(is_error(gagp::execute_bytecode_cpu(program, {}, boundary), code),
               label + ": wrong error at its exact fuel boundary");
}

bool test_dispatch_precedence() {
  if (!exact_error_boundary(call_program(999, -1), 1, ErrCode::Type,
                            "invalid argc precedes unknown id") ||
      !exact_error_boundary(call_program(999, 1), 1, ErrCode::Value,
                            "stack underflow precedes unknown id") ||
      !exact_error_boundary(call_program(999, 1, {Value::from_int(4)}), 2,
                            ErrCode::Name,
                            "unknown id after sufficient operands") ||
      !exact_error_boundary(
          call_program(static_cast<int>(BuiltinId::Abs), 2,
                       {Value::from_int(1), Value::from_int(2)}),
          3, ErrCode::Type, "valid builtin wrong arity") ||
      !exact_error_boundary(
          call_program(static_cast<int>(BuiltinId::Abs), 1,
                       {Value::from_bool(true)}),
          2, ErrCode::Type, "valid builtin wrong type")) {
    return false;
  }
  return exact_error_boundary(
      call_program(static_cast<int>(BuiltinId::Abs), 0), 1,
      ErrCode::Type, "zero arguments with empty stack");
}

bool test_chained_calls_preserve_stack_prefix() {
  BytecodeProgram program;
  program.consts = {Value::from_int(100), Value::from_int(-3)};
  program.code = {
      ins_a(Opcode::PushConst, 0), ins_a(Opcode::PushConst, 1),
      ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Abs), 1),
      ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Min), 2),
      ins(Opcode::Return),
  };
  const ExecResult exact = gagp::execute_bytecode_cpu(program, {}, 5);
  return check(!exact.is_error && exact.value.tag == ValueTag::Int &&
                   exact.value.i == 3,
               "chained calls consumed or reordered the preceding stack value") &&
         check(is_error(gagp::execute_bytecode_cpu(program, {}, 4),
                        ErrCode::Timeout),
               "chained calls changed their exact fuel boundary");
}

bool test_builtin_after_stack_spill() {
  BytecodeProgram program;
  for (int value = 1; value <= 32; ++value) {
    program.consts.push_back(Value::from_int(value == 32 ? -value : value));
    program.code.push_back(ins_a(Opcode::PushConst, value - 1));
  }
  program.code.push_back(ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Abs), 1));
  for (int count = 1; count < 32; ++count) program.code.push_back(ins(Opcode::Add));
  program.code.push_back(ins(Opcode::Return));
  const int fuel = static_cast<int>(program.code.size());
  const auto result = gagp::execute_bytecode_cpu(program, {}, fuel);
  return check(!result.is_error && result.value.tag == ValueTag::Int && result.value.i == 528,
               "builtin call or stack growth corrupted live operands") &&
         check(is_error(gagp::execute_bytecode_cpu(program, {}, fuel - 1), ErrCode::Timeout),
               "spilled stack changed the fuel boundary");
}

template <std::size_t N>
bool overloads_match(BuiltinId id, const std::array<Value, N>& args,
                     const std::string& label) {
  const std::vector<Value> copied(args.begin(), args.end());
  const BuiltinResult vector_result = gagp::builtin_call(id, copied);
  const BuiltinResult pointer_result =
      gagp::builtin_call(id, args.data(), args.size());
  return check(same_builtin_result(vector_result, pointer_result),
               label + ": pointer/count and vector overloads differ");
}

bool test_pointer_count_overload() {
  if (!overloads_match(BuiltinId::Abs,
                       std::array<Value, 1>{Value::from_int(-9)},
                       "scalar call")) return false;

  const Value left = gagp::payload::make_string_value("ab");
  const Value right = gagp::payload::make_string_value("c");
  if (!overloads_match(BuiltinId::Concat,
                       std::array<Value, 2>{left, right},
                       "String payload call")) return false;

  const Value list = gagp::payload::make_int_list_value({Value::from_int(1)});
  if (!overloads_match(BuiltinId::Append,
                       std::array<Value, 2>{list, Value::from_int(2)},
                       "typed-list payload call")) return false;

  const Value unresolved_left = gagp::payload::make_string_value("left");
  const Value unresolved_right = gagp::payload::make_string_value("right");
  gagp::payload::clear();
  if (!overloads_match(
          BuiltinId::Concat,
          std::array<Value, 2>{unresolved_left, unresolved_right},
          "fallback call")) return false;

  const BuiltinResult vector_empty =
      gagp::builtin_call(BuiltinId::Abs, std::vector<Value>{});
  const BuiltinResult pointer_empty =
      gagp::builtin_call(BuiltinId::Abs, nullptr, 0);
  return check(vector_empty.is_error && pointer_empty.is_error &&
                   vector_empty.err.code == ErrCode::Type &&
                   pointer_empty.err.code == ErrCode::Type,
               "null pointer with zero argc was not safely rejected by builtin type checking");
}

bool int_result(const ExecResult& result, std::int64_t expected) {
  return !result.is_error && result.value.tag == ValueTag::Int &&
         result.value.i == expected;
}

bool test_local_slot_storage_boundaries() {
  constexpr std::array<int, 5> kSizes{0, 1, 16, 17, 32};
  for (const int count : kSizes) {
    const std::string label = "n_locals=" + std::to_string(count);

    BytecodeProgram ignored_input;
    ignored_input.n_locals = count;
    ignored_input.consts = {Value::from_int(9)};
    ignored_input.code = {ins_a(Opcode::PushConst, 0), ins(Opcode::Return)};
    if (!check(int_result(gagp::execute_bytecode_cpu(
                              ignored_input,
                              {{-1, Value::from_int(7)},
                               {count, Value::from_int(8)}},
                              2),
                          9),
               label + ": out-of-range indexed input was not ignored")) {
      return false;
    }

    BytecodeProgram out_of_bounds_load;
    out_of_bounds_load.n_locals = count;
    out_of_bounds_load.code = {ins_a(Opcode::Load, count)};
    if (!exact_error_boundary(out_of_bounds_load, 1, ErrCode::Name,
                              label + " out-of-bounds load")) return false;

    BytecodeProgram out_of_bounds_store;
    out_of_bounds_store.n_locals = count;
    out_of_bounds_store.consts = {Value::from_int(11)};
    out_of_bounds_store.code = {ins_a(Opcode::PushConst, 0),
                                ins_a(Opcode::Store, count)};
    if (!exact_error_boundary(out_of_bounds_store, 2, ErrCode::Name,
                              label + " out-of-bounds store")) return false;

    if (count == 0) continue;
    const int last = count - 1;
    BytecodeProgram input_load;
    input_load.n_locals = count;
    input_load.code = {ins_a(Opcode::Load, last), ins(Opcode::Return)};
    const std::vector<std::pair<int, Value>> input{
        {last, Value::from_int(1000 + count)}};
    if (!check(int_result(gagp::execute_bytecode_cpu(input_load, input, 2),
                          1000 + count),
               label + ": last-slot input did not load exactly") ||
        !check(is_error(gagp::execute_bytecode_cpu(input_load, input, 1),
                        ErrCode::Timeout),
               label + ": input load changed its fuel boundary")) {
      return false;
    }

    BytecodeProgram stored;
    stored.n_locals = count;
    stored.consts = {Value::from_int(2000 + count)};
    stored.code = {ins_a(Opcode::PushConst, 0), ins_a(Opcode::Store, last),
                   ins_a(Opcode::Load, last), ins(Opcode::Return)};
    if (!check(int_result(gagp::execute_bytecode_cpu(stored, {}, 4),
                          2000 + count),
               label + ": last-slot store/load did not round-trip") ||
        !check(is_error(gagp::execute_bytecode_cpu(stored, {}, 3),
                        ErrCode::Timeout),
               label + ": store/load changed its fuel boundary")) {
      return false;
    }

    BytecodeProgram unset;
    unset.n_locals = count;
    unset.code = {ins_a(Opcode::Load, last)};
    if (!exact_error_boundary(unset, 1, ErrCode::Name,
                              label + " uninitialized load")) return false;

    const ExecResult primed =
        gagp::execute_bytecode_cpu(input_load, input, 2);
    const ExecResult repeated =
        gagp::execute_bytecode_cpu(input_load, {}, 2);
    if (!check(int_result(primed, 1000 + count),
               label + ": priming execution failed") ||
        !check(is_error(repeated, ErrCode::Name),
               label + ": repeated execution reused stale is_set state")) {
      return false;
    }
  }
  return true;
}
}  // namespace

int main() {
  gagp::payload::clear();
  if (!test_dispatch_precedence()) return 1;
  if (!test_chained_calls_preserve_stack_prefix()) return 1;
  if (!test_builtin_after_stack_spill()) return 1;
  if (!test_pointer_count_overload()) return 1;
  if (!test_local_slot_storage_boundaries()) return 1;
  std::cout << "gagp_test_builtin_arguments: OK\n";
  return 0;
}
