#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/core/errors.hpp"
#include "gagp/core/value.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/input_spec.hpp"
#include "gagp/evolution/transition/linear_rec.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace {

using Expr = std::vector<gagp::evo::AstNode>;
using gagp::BytecodeProgram;
using gagp::ErrCode;
using gagp::ExecResult;
using gagp::Value;
using gagp::ValueTag;
using gagp::evo::AstProgram;
using gagp::evo::InputSpec;
using gagp::evo::LexicalBinding;
using gagp::evo::LexicalRegion;
using gagp::evo::LinearRecBinders;
using gagp::evo::ListTypeTag;
using gagp::evo::NodeKind;
using gagp::evo::ProgramGenome;
using gagp::evo::RType;
using gagp::evo::TraversalDirection;
using gagp::evo::TraversalSpec;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

void append(Expr* target, const Expr& child) {
  target->insert(target->end(), child.begin(), child.end());
}

Expr leaf(NodeKind kind, int i0 = 0) { return {{kind, i0, 0}}; }

Expr expression(NodeKind kind, std::initializer_list<Expr> children) {
  Expr out{{kind, 0, 0}};
  for (const Expr& child : children) append(&out, child);
  return out;
}

Expr binary(NodeKind kind, const Expr& lhs, const Expr& rhs) {
  return expression(kind, {lhs, rhs});
}

Expr linear_rec(const Expr& source, const Expr& start, const Expr& empty,
                const Expr& step, const Expr& last) {
  return expression(NodeKind::LINEAR_REC, {source, start, empty, step, last});
}

Expr map_list(int binder, ListTypeTag output_type, const Expr& source,
              const Expr& body) {
  Expr result = expression(NodeKind::MAP_LIST, {source, body});
  result[0].i0 = binder;
  result[0].i1 = static_cast<int>(output_type);
  return result;
}

Expr filter_list(int binder, const Expr& source, const Expr& predicate) {
  Expr result = expression(NodeKind::FILTER_LIST, {source, predicate});
  result[0].i0 = binder;
  return result;
}

AstProgram return_program(const Expr& result, std::vector<Value> constants,
                          std::vector<std::string> names = {}) {
  AstProgram ast;
  ast.consts = std::move(constants);
  ast.names = std::move(names);
  ast.nodes = {{NodeKind::PROGRAM, 0, 0}, {NodeKind::BLOCK_CONS, 0, 0},
               {NodeKind::RETURN, 0, 0}};
  append(&ast.nodes, result);
  ast.nodes.push_back({NodeKind::BLOCK_NIL, 0, 0});
  return ast;
}

ProgramGenome genome(AstProgram ast) {
  ProgramGenome result;
  result.ast = std::move(ast);
  result.meta = gagp::evo::build_genome_meta(result.ast);
  return result;
}

std::vector<std::string> input_names(const std::vector<InputSpec>& inputs) {
  std::vector<std::string> result;
  result.reserve(inputs.size());
  for (const auto& input : inputs) result.push_back(input.name);
  return result;
}

BytecodeProgram compile(const ProgramGenome& program,
                        const std::vector<InputSpec>& inputs) {
  const auto verified = gagp::evo::verify_ast(program.ast, inputs);
  if (!verified.ok) {
    throw std::runtime_error(std::string("fixture verification failed: ") +
                             gagp::evo::verify_code_name(verified.diagnostic.code) +
                             " " + verified.diagnostic.message);
  }
  return gagp::evo::compile_for_eval(program, verified.verified,
                                     input_names(inputs));
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

bool same_result(const ExecResult& lhs, const ExecResult& rhs) {
  if (lhs.is_error != rhs.is_error) return false;
  return lhs.is_error ? lhs.err.code == rhs.err.code
                      : exact_value(lhs.value, rhs.value);
}

bool has_linear_rec(const ProgramGenome& program) {
  for (const auto& node : program.ast.nodes) {
    if (node.kind == NodeKind::LINEAR_REC) return true;
  }
  return false;
}

int first_non_timeout(const BytecodeProgram& program,
                      const std::vector<std::pair<int, Value>>& inputs) {
  for (int fuel = 0; fuel <= 4000; ++fuel) {
    const ExecResult result = gagp::execute_bytecode_cpu(program, inputs, fuel);
    if (!result.is_error || result.err.code != ErrCode::Timeout) return fuel;
  }
  return -1;
}

bool differential(const ProgramGenome& original,
                  const std::vector<InputSpec>& input_types,
                  const std::vector<std::pair<int, Value>>& runtime_inputs,
                  const std::string& label, int expected_min = -1) {
  const BytecodeProgram before = compile(original, input_types);
  const ProgramGenome lowered =
      gagp::evo::transition::lower_linear_rec(original, input_types);
  if (!check(!has_linear_rec(lowered), label + ": lowering left LINEAR_REC") ||
      !check(lowered.ast.linear_rec_binders.empty(),
             label + ": lowering left LinearRec binder metadata") ||
      !check(!lowered.ast.fuel_specs.empty(),
             label + ": lowering did not preserve costs as source events")) {
    return false;
  }
  const BytecodeProgram after = compile(lowered, input_types);
  const int minimum = first_non_timeout(before, runtime_inputs);
  if (!check(minimum >= 0, label + ": no finite original fuel boundary")) {
    return false;
  }
  if (expected_min >= 0 &&
      !check(minimum == expected_min,
             label + ": frozen minimum changed from " +
                 std::to_string(expected_min) + " to " +
                 std::to_string(minimum))) {
    return false;
  }
  for (int fuel = 0; fuel <= minimum + 3; ++fuel) {
    const ExecResult a =
        gagp::execute_bytecode_cpu(before, runtime_inputs, fuel);
    const ExecResult b =
        gagp::execute_bytecode_cpu(after, runtime_inputs, fuel);
    if (!check(same_result(a, b),
               label + ": result differs at fuel " + std::to_string(fuel))) {
      return false;
    }
  }
  return true;
}

ProgramGenome frozen_empty() {
  const Value xs = gagp::payload::make_int_list_value({});
  const Expr error = binary(NodeKind::MOD, leaf(NodeKind::CONST, 3),
                            leaf(NodeKind::CONST, 4));
  AstProgram ast = return_program(
      linear_rec(leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
                 leaf(NodeKind::CONST, 2), error, error),
      {xs, Value::from_int(5), Value::from_int(42), Value::from_int(1),
       Value::from_int(0)},
      {"u", "v", "i"});
  ast.linear_rec_binders = {{3, 0, 1, 2}};
  return genome(std::move(ast));
}

ProgramGenome frozen_singleton() {
  const Value xs =
      gagp::payload::make_int_list_value({Value::from_int(7)});
  const Expr step = binary(NodeKind::ADD, leaf(NodeKind::BOUND_VAR, 0),
                           leaf(NodeKind::BOUND_VAR, 1));
  const Expr last = binary(
      NodeKind::ADD,
      binary(NodeKind::MUL, leaf(NodeKind::BOUND_VAR, 0),
             leaf(NodeKind::CONST, 3)),
      leaf(NodeKind::BOUND_VAR, 2));
  AstProgram ast = return_program(
      linear_rec(leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
                 leaf(NodeKind::CONST, 2), step, last),
      {xs, Value::from_int(5), Value::from_int(0), Value::from_int(10)},
      {"u", "v", "i"});
  ast.linear_rec_binders = {{3, 0, 1, 2}};
  return genome(std::move(ast));
}

ProgramGenome frozen_three() {
  const Value xs = gagp::payload::make_int_list_value(
      {Value::from_int(1), Value::from_int(2), Value::from_int(3)});
  const Expr step = binary(
      NodeKind::ADD,
      binary(NodeKind::MUL, leaf(NodeKind::BOUND_VAR, 1),
             leaf(NodeKind::CONST, 3)),
      leaf(NodeKind::BOUND_VAR, 0));
  const Expr last = binary(
      NodeKind::ADD,
      binary(NodeKind::MUL, leaf(NodeKind::BOUND_VAR, 0),
             leaf(NodeKind::CONST, 4)),
      leaf(NodeKind::BOUND_VAR, 2));
  AstProgram ast = return_program(
      linear_rec(leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
                 leaf(NodeKind::CONST, 2), step, last),
      {xs, Value::from_int(4), Value::from_int(0), Value::from_int(10),
       Value::from_int(100)},
      {"u", "v", "i"});
  ast.linear_rec_binders = {{3, 0, 1, 2}};
  return genome(std::move(ast));
}

bool test_frozen_boundaries() {
  return differential(frozen_empty(), {}, {}, "frozen empty", 16) &&
         differential(frozen_singleton(), {}, {}, "frozen singleton", 37) &&
         differential(frozen_three(), {}, {}, "frozen three-element", 83);
}

bool test_lazy_errors() {
  if (!differential(frozen_empty(), {}, {}, "empty skips step and last")) {
    return false;
  }

  const Value singleton =
      gagp::payload::make_int_list_value({Value::from_int(9)});
  const Expr error = binary(NodeKind::MOD, leaf(NodeKind::CONST, 3),
                            leaf(NodeKind::CONST, 4));
  AstProgram lazy = return_program(
      linear_rec(leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1), error,
                 error, leaf(NodeKind::BOUND_VAR, 0)),
      {singleton, Value::from_int(0), Value::from_int(0), Value::from_int(1),
       Value::from_int(0)},
      {"u", "v", "i"});
  lazy.linear_rec_binders = {{3, 0, 1, 2}};
  if (!differential(genome(std::move(lazy)), {}, {},
                    "singleton skips empty and step")) {
    return false;
  }

  const Value three = gagp::payload::make_int_list_value(
      {Value::from_int(1), Value::from_int(2), Value::from_int(3)});
  AstProgram failing = return_program(
      linear_rec(leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
                 leaf(NodeKind::CONST, 2), error,
                 leaf(NodeKind::BOUND_VAR, 0)),
      {three, Value::from_int(0), Value::from_int(77), Value::from_int(1),
       Value::from_int(0)},
      {"u", "v", "i"});
  failing.linear_rec_binders = {{3, 0, 1, 2}};
  return differential(genome(std::move(failing)), {}, {},
                      "selected step error and fuel precedence");
}

ProgramGenome index_program(std::int64_t start) {
  const Value xs = gagp::payload::make_int_list_value(
      {Value::from_int(1), Value::from_int(2), Value::from_int(3)});
  AstProgram ast = return_program(
      linear_rec(leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
                 leaf(NodeKind::CONST, 2), leaf(NodeKind::BOUND_VAR, 1),
                 leaf(NodeKind::BOUND_VAR, 2)),
      {xs, Value::from_int(start), Value::from_int(0)}, {"u", "v", "i"});
  ast.linear_rec_binders = {{3, 0, 1, 2}};
  return genome(std::move(ast));
}

bool test_start_overflow_and_precision() {
  return differential(index_program(std::numeric_limits<std::int64_t>::max()),
                      {}, {}, "wrapping start index") &&
         differential(index_program(INT64_C(9007199254740993)), {}, {},
                      "start index above exact double precision");
}

bool test_nested_shadowing() {
  const Expr inner = linear_rec(
      leaf(NodeKind::CONST, 3), leaf(NodeKind::CONST, 4),
      leaf(NodeKind::CONST, 5),
      binary(NodeKind::ADD, leaf(NodeKind::BOUND_VAR, 0),
             leaf(NodeKind::BOUND_VAR, 1)),
      leaf(NodeKind::BOUND_VAR, 0));
  const Expr outer_step = binary(
      NodeKind::ADD,
      binary(NodeKind::ADD, leaf(NodeKind::BOUND_VAR, 1), inner),
      leaf(NodeKind::BOUND_VAR, 0));
  const Value outer = gagp::payload::make_int_list_value(
      {Value::from_int(1), Value::from_int(2)});
  const Value inner_xs =
      gagp::payload::make_int_list_value({Value::from_int(10)});
  AstProgram ast = return_program(
      linear_rec(leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
                 leaf(NodeKind::CONST, 2), outer_step,
                 leaf(NodeKind::BOUND_VAR, 0)),
      {outer, Value::from_int(0), Value::from_int(0), inner_xs,
       Value::from_int(0), Value::from_int(0)},
      {"u", "v", "i"});
  std::vector<std::size_t> linear_nodes;
  for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
    if (ast.nodes[i].kind == NodeKind::LINEAR_REC) linear_nodes.push_back(i);
  }
  if (!check(linear_nodes.size() == 2, "nested fixture shape")) return false;
  ast.linear_rec_binders = {{linear_nodes[0], 0, 1, 2},
                            {linear_nodes[1], 0, 1, 2}};
  return differential(genome(std::move(ast)), {}, {},
                      "nested LinearRec binder shadowing");
}

bool test_nested_map_filter_shadowing() {
  // The map/filter sources see the outer LinearRec element. Their body and
  // predicate deliberately reuse outer binder names and must shadow them.
  const Expr mapped = map_list(
      0, ListTypeTag::Int,
      expression(NodeKind::CALL_SINGLETON,
                 {binary(NodeKind::ADD, leaf(NodeKind::BOUND_VAR, 0),
                         leaf(NodeKind::CONST, 3))}),
      binary(NodeKind::ADD, leaf(NodeKind::BOUND_VAR, 0),
             leaf(NodeKind::CONST, 3)));
  const Expr selected = filter_list(
      1, mapped, binary(NodeKind::GT, leaf(NodeKind::BOUND_VAR, 1),
                        leaf(NodeKind::CONST, 4)));
  const Expr inner = linear_rec(
      selected, leaf(NodeKind::BOUND_VAR, 1), leaf(NodeKind::BOUND_VAR, 1),
      binary(NodeKind::ADD, leaf(NodeKind::BOUND_VAR, 0),
             leaf(NodeKind::BOUND_VAR, 1)),
      binary(NodeKind::ADD, leaf(NodeKind::BOUND_VAR, 0),
             leaf(NodeKind::BOUND_VAR, 2)));
  const Value outer = gagp::payload::make_int_list_value(
      {Value::from_int(2), Value::from_int(3)});
  AstProgram ast = return_program(
      linear_rec(leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
                 leaf(NodeKind::CONST, 1), inner,
                 leaf(NodeKind::BOUND_VAR, 0)),
      {outer, Value::from_int(0), Value::from_int(1), Value::from_int(10),
       Value::from_int(15)},
      {"u", "v", "i"});
  std::vector<std::size_t> linear_nodes;
  for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
    if (ast.nodes[i].kind == NodeKind::LINEAR_REC) linear_nodes.push_back(i);
  }
  if (!check(linear_nodes.size() == 2,
             "map/filter nested fixture shape")) return false;
  ast.linear_rec_binders = {{linear_nodes[0], 0, 1, 2},
                            {linear_nodes[1], 0, 1, 2}};
  if (!differential(genome(ast), {}, {},
                    "nested map/filter binder shadowing")) return false;
  ast.consts[4] = Value::from_int(100);
  return differential(genome(std::move(ast)), {}, {},
                      "nested empty case retains outer accumulator");
}

ProgramGenome native_let_capture_program(bool empty_source) {
  const Expr recursion = linear_rec(
      leaf(NodeKind::REGION_VAR, 0), leaf(NodeKind::REGION_VAR, 1),
      leaf(NodeKind::CONST, 2),
      binary(NodeKind::ADD, leaf(NodeKind::BOUND_VAR, 0),
             leaf(NodeKind::BOUND_VAR, 1)),
      binary(NodeKind::ADD, leaf(NodeKind::BOUND_VAR, 0),
             leaf(NodeKind::BOUND_VAR, 2)));
  const Expr inner_let = expression(
      NodeKind::LET_REGION, {leaf(NodeKind::CONST, 1), recursion});
  const Expr outer_let = expression(
      NodeKind::LET_REGION, {leaf(NodeKind::CONST, 0), inner_let});
  const Value source = empty_source
      ? gagp::payload::make_int_list_value({})
      : gagp::payload::make_int_list_value(
            {Value::from_int(4), Value::from_int(5)});
  AstProgram ast = return_program(
      outer_let, {source, Value::from_int(10), Value::from_int(77)},
      {"u", "v", "i"});
  std::vector<std::size_t> lets;
  std::size_t recursion_node = 0;
  for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
    if (ast.nodes[i].kind == NodeKind::LET_REGION) lets.push_back(i);
    if (ast.nodes[i].kind == NodeKind::LINEAR_REC) recursion_node = i;
  }
  if (lets.size() != 2 || recursion_node == 0)
    throw std::logic_error("native let capture fixture shape");
  // The native binder IDs overlap the legacy name IDs below. The namespaces
  // remain distinct, while IDs 0 and 1 are reserved from adapter allocation.
  ast.lexical_regions = {
      LexicalRegion{lets[0], 1, {LexicalBinding{0, RType::IntList}}},
      LexicalRegion{lets[1], 1, {LexicalBinding{1, RType::Int}}},
  };
  ast.linear_rec_binders = {{recursion_node, 0, 1, 2}};
  return genome(std::move(ast));
}

bool test_native_let_enclosing_captures() {
  return differential(native_let_capture_program(false), {}, {},
                      "native Let nonempty captures") &&
         differential(native_let_capture_program(true), {}, {},
                      "native Let empty captures");
}

ProgramGenome native_traverse_capture_program() {
  const Expr inner_source = binary(
      NodeKind::CALL_APPEND, leaf(NodeKind::REGION_VAR, 2),
      leaf(NodeKind::REGION_VAR, 0));
  const Expr inner_step = binary(
      NodeKind::CALL_APPEND, leaf(NodeKind::BOUND_VAR, 1),
      leaf(NodeKind::BOUND_VAR, 0));
  const Expr inner_last = expression(
      NodeKind::CALL_SINGLETON,
      {binary(NodeKind::ADD, leaf(NodeKind::BOUND_VAR, 0),
              leaf(NodeKind::BOUND_VAR, 2))});
  const Expr recursion = linear_rec(
      inner_source, leaf(NodeKind::REGION_VAR, 1),
      leaf(NodeKind::REGION_VAR, 2), inner_step, inner_last);
  const Expr traversal = expression(
      NodeKind::TRAVERSE,
      {leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
       leaf(NodeKind::CONST, 2), recursion});
  AstProgram ast = return_program(
      traversal,
      {gagp::payload::make_int_list_value(
           {Value::from_int(2), Value::from_int(3)}),
       Value::from_int(0),
       gagp::payload::make_int_list_value({Value::from_int(9)})},
      {"u", "v", "i"});
  std::size_t traversal_node = 0;
  std::size_t recursion_node = 0;
  for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
    if (ast.nodes[i].kind == NodeKind::TRAVERSE) traversal_node = i;
    if (ast.nodes[i].kind == NodeKind::LINEAR_REC) recursion_node = i;
  }
  if (traversal_node == 0 || recursion_node == 0)
    throw std::logic_error("native traversal capture fixture shape");
  ast.lexical_regions = {LexicalRegion{
      traversal_node, 3,
      {{0, RType::Int}, {1, RType::Int}, {2, RType::IntList}}}};
  ast.traversal_specs = {
      TraversalSpec{traversal_node, TraversalDirection::Forward}};
  ast.linear_rec_binders = {{recursion_node, 0, 1, 2}};
  return genome(std::move(ast));
}

bool test_native_traversal_enclosing_captures() {
  return differential(native_traverse_capture_program(), {}, {},
                      "native traversal step captures");
}

struct TypedCase {
  RType list_type;
  Value source;
  Value empty;
  const char* label;
};

bool test_typed_inputs_and_sequence_results() {
  const Value sa = gagp::payload::make_string_value("a");
  const Value embedded =
      gagp::payload::make_string_value(std::string("b\0c", 3));
  const std::vector<TypedCase> cases{
      {RType::IntList,
       gagp::payload::make_int_list_value(
           {Value::from_int(4), Value::from_int(-2)}),
       gagp::payload::make_int_list_value({}), "IntList"},
      {RType::FloatList,
       gagp::payload::make_float_list_value(
           {Value::from_float(1.25), Value::from_float(-0.0)}),
       gagp::payload::make_float_list_value({}), "FloatList"},
      {RType::StringList,
       gagp::payload::make_string_list_value({sa, embedded}),
       gagp::payload::make_string_list_value({}), "StringList"},
  };
  for (const auto& item : cases) {
    const Expr step = binary(NodeKind::CALL_PREPEND,
                             leaf(NodeKind::BOUND_VAR, 3),
                             leaf(NodeKind::BOUND_VAR, 2));
    const Expr last =
        expression(NodeKind::CALL_SINGLETON, {leaf(NodeKind::BOUND_VAR, 2)});
    AstProgram ast = return_program(
        linear_rec(leaf(NodeKind::VAR, 0), leaf(NodeKind::VAR, 1),
                   leaf(NodeKind::CONST, 0), step, last),
        {item.empty}, {"xs", "start", "u", "v", "i"});
    ast.linear_rec_binders = {{3, 2, 3, 4}};
    const std::vector<InputSpec> types{{"xs", item.list_type},
                                       {"start", RType::Int}};
    const std::vector<std::pair<int, Value>> values{
        {0, item.source}, {1, Value::from_int(7)}};
    if (!differential(genome(std::move(ast)), types, values,
                      std::string(item.label) + " input and result")) {
      return false;
    }
  }
  return true;
}

bool unresolved_case(Value source, Value empty, const std::string& label) {
  AstProgram ast = return_program(
      linear_rec(leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
                 leaf(NodeKind::CONST, 2), leaf(NodeKind::BOUND_VAR, 1),
                 leaf(NodeKind::BOUND_VAR, 0)),
      {source, Value::from_int(0), empty}, {"u", "v", "i"});
  ast.linear_rec_binders = {{3, 0, 1, 2}};
  const ProgramGenome original = genome(std::move(ast));
  gagp::payload::clear();
  if (!differential(original, {}, {}, label)) return false;
  const BytecodeProgram bytecode = compile(original, {});
  const int minimum = first_non_timeout(bytecode, {});
  const ExecResult result = gagp::execute_bytecode_cpu(bytecode, {}, minimum);
  return check(!result.is_error && result.value.tag == ValueTag::FallbackToken,
               label + ": unresolved index did not return a fallback token");
}

bool test_unresolved_typed_list_tokens() {
  if (!unresolved_case(
          gagp::payload::make_int_list_value(
              {Value::from_int(4), Value::from_int(5)}),
          Value::from_int(0), "unresolved IntList token")) return false;
  if (!unresolved_case(
          gagp::payload::make_float_list_value(
              {Value::from_float(1.25), Value::from_float(2.5)}),
          Value::from_float(0.0), "unresolved FloatList token")) return false;
  const Value a = gagp::payload::make_string_value("a");
  const Value b = gagp::payload::make_string_value("b");
  return unresolved_case(
      gagp::payload::make_string_list_value({a, b}),
      gagp::payload::make_string_value(""), "unresolved StringList token");
}

bool empty_typed_case(RType list_type, Value empty, const std::string& label) {
  AstProgram ast = return_program(
      linear_rec(leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
                 leaf(NodeKind::CONST, 0), leaf(NodeKind::BOUND_VAR, 1),
                 leaf(NodeKind::CONST, 0)),
      {empty, Value::from_int(0)}, {"u", "v", "i"});
  ast.linear_rec_binders = {{3, 0, 1, 2}};
  const ProgramGenome original = genome(std::move(ast));
  if (!differential(original, {}, {}, label)) return false;
  const BytecodeProgram bytecode = compile(original, {});
  const int minimum = first_non_timeout(bytecode, {});
  const ExecResult result = gagp::execute_bytecode_cpu(bytecode, {}, minimum);
  std::vector<Value> values;
  return check(!result.is_error &&
                   ((list_type == RType::IntList &&
                     result.value.tag == ValueTag::IntList) ||
                    (list_type == RType::FloatList &&
                     result.value.tag == ValueTag::FloatList) ||
                    (list_type == RType::StringList &&
                     result.value.tag == ValueTag::StringList)) &&
                   gagp::payload::lookup_list(result.value, &values) &&
                   values.empty(),
               label + ": empty source did not preserve typed empty result");
}

bool test_empty_typed_list_results() {
  return empty_typed_case(RType::IntList,
                          gagp::payload::make_int_list_value({}),
                          "empty IntList result") &&
         empty_typed_case(RType::FloatList,
                          gagp::payload::make_float_list_value({}),
                          "empty FloatList result") &&
         empty_typed_case(RType::StringList,
                          gagp::payload::make_string_list_value({}),
                          "empty StringList result");
}

ProgramGenome runtime_input_program() {
  AstProgram ast = return_program(
      linear_rec(leaf(NodeKind::VAR, 0), leaf(NodeKind::VAR, 1),
                 leaf(NodeKind::CONST, 0), leaf(NodeKind::BOUND_VAR, 3),
                 leaf(NodeKind::BOUND_VAR, 2)),
      {Value::from_int(0)}, {"xs", "start", "u", "v", "i"});
  ast.linear_rec_binders = {{3, 2, 3, 4}};
  return genome(std::move(ast));
}

bool test_runtime_tag_mismatches() {
  const std::vector<InputSpec> types{{"xs", RType::IntList},
                                     {"start", RType::Int}};
  const Value xs = gagp::payload::make_int_list_value({Value::from_int(1)});
  if (!differential(runtime_input_program(), types,
                    {{0, xs}, {1, Value::from_bool(false)}},
                    "runtime mismatched start tag")) {
    return false;
  }
  return differential(runtime_input_program(), types,
                      {{0, Value::from_int(9)}, {1, Value::from_int(0)}},
                      "runtime mismatched list tag");
}

bool test_payload_lifetime_after_source_scope() {
  BytecodeProgram before;
  ProgramGenome lowered;
  Value nested_a = Value::invalid();
  Value nested_b = Value::invalid();
  Value garbage = Value::invalid();
  {
    nested_a = gagp::payload::make_string_value("left");
    nested_b = gagp::payload::make_string_value("right");
    const Value xs =
        gagp::payload::make_string_list_value({nested_a, nested_b});
    const Value empty = gagp::payload::make_string_value("empty");
    garbage = gagp::payload::make_string_value("unrelated garbage");
    (void)gagp::payload::make_string_list_value({garbage});
    AstProgram ast = return_program(
        linear_rec(leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
                   leaf(NodeKind::CONST, 2),
                   binary(NodeKind::CALL_CONCAT,
                          leaf(NodeKind::BOUND_VAR, 0),
                          leaf(NodeKind::BOUND_VAR, 1)),
                   leaf(NodeKind::BOUND_VAR, 0)),
        {xs, Value::from_int(0), empty}, {"u", "v", "i"});
    ast.linear_rec_binders = {{3, 0, 1, 2}};
    const ProgramGenome original = genome(std::move(ast));
    lowered = gagp::evo::transition::lower_linear_rec(original);
    before = compile(original, {});
  }
  gagp::payload::retain_only(lowered.ast.consts);
  std::string nested_text;
  if (!check(gagp::payload::lookup_string(nested_a, &nested_text) &&
                 nested_text == "left" &&
                 gagp::payload::lookup_string(nested_b, &nested_text) &&
                 nested_text == "right",
             "payload sweep dropped strings nested in a StringList constant") ||
      !check(!gagp::payload::lookup_string(garbage, &nested_text),
             "payload sweep retained unrelated garbage")) return false;
  const BytecodeProgram after = compile(lowered, {});
  const int minimum = first_non_timeout(before, {});
  if (!check(minimum >= 0, "payload lifetime fixture boundary")) return false;
  for (int fuel = 0; fuel <= minimum + 3; ++fuel) {
    if (!check(same_result(gagp::execute_bytecode_cpu(before, {}, fuel),
                           gagp::execute_bytecode_cpu(after, {}, fuel)),
               "payload-backed lowered constant differs at fuel " +
                   std::to_string(fuel))) {
      return false;
    }
  }
  const ExecResult result = gagp::execute_bytecode_cpu(after, {}, minimum);
  std::string text;
  return check(!result.is_error && result.value.tag == ValueTag::String &&
                   gagp::payload::lookup_string(result.value, &text) &&
                   text == "leftright",
               "lowered genome should retain payload-backed constants and result");
}

}  // namespace

int main() {
  gagp::payload::clear();
  if (!test_frozen_boundaries()) return 1;
  if (!test_lazy_errors()) return 1;
  if (!test_start_overflow_and_precision()) return 1;
  if (!test_nested_shadowing()) return 1;
  if (!test_nested_map_filter_shadowing()) return 1;
  if (!test_native_let_enclosing_captures()) return 1;
  if (!test_native_traversal_enclosing_captures()) return 1;
  if (!test_typed_inputs_and_sequence_results()) return 1;
  if (!test_unresolved_typed_list_tokens()) return 1;
  if (!test_empty_typed_list_results()) return 1;
  if (!test_runtime_tag_mismatches()) return 1;
  if (!test_payload_lifetime_after_source_scope()) return 1;
  std::cout << "gagp_test_linear_rec_transition: OK\n";
  return 0;
}
