#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/core/builtin.hpp"
#include "gagp/core/bytecode.hpp"
#include "gagp/core/errors.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/fuel_events.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace {
using gagp::BuiltinId;
using gagp::BytecodeProgram;
using gagp::ErrCode;
using gagp::ExecResult;
using gagp::Opcode;
using gagp::Value;
using gagp::ValueTag;
using gagp::evo::AstNode;
using gagp::evo::AstProgram;
using gagp::evo::FuelCharge;
using gagp::evo::FuelEvent;
using gagp::evo::InputSpec;
using gagp::evo::LexicalBinding;
using gagp::evo::LexicalRegion;
using gagp::evo::NodeFuelSpec;
using gagp::evo::NodeKind;
using gagp::evo::ProgramGenome;
using gagp::evo::RType;
using gagp::evo::TraversalDirection;
using gagp::evo::TraversalSpec;

struct Expr {
  std::vector<AstNode> nodes;
  std::vector<LexicalRegion> regions;
  std::vector<TraversalSpec> traversals;
  std::vector<NodeFuelSpec> fuel;
};

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << '\n';
  return condition;
}

Expr leaf(NodeKind kind, int i0 = 0) { return {{{kind, i0, 0}}, {}, {}, {}}; }

void append(Expr* target, Expr child) {
  const auto shift = target->nodes.size();
  for (auto& row : child.regions) row.node_index += shift;
  for (auto& row : child.traversals) row.node_index += shift;
  for (auto& row : child.fuel) row.node_index += shift;
  target->nodes.insert(target->nodes.end(), child.nodes.begin(), child.nodes.end());
  target->regions.insert(target->regions.end(), child.regions.begin(), child.regions.end());
  target->traversals.insert(target->traversals.end(), child.traversals.begin(), child.traversals.end());
  target->fuel.insert(target->fuel.end(), child.fuel.begin(), child.fuel.end());
}

Expr expression(NodeKind kind, std::initializer_list<Expr> children) {
  Expr out = leaf(kind);
  for (auto child : children) append(&out, std::move(child));
  return out;
}

void profile(Expr* expression, std::initializer_list<FuelCharge> charges) {
  expression->fuel.insert(expression->fuel.begin(), NodeFuelSpec{0, charges});
}

Expr let(Expr initializer, Expr body, int binder, RType type) {
  Expr out = expression(NodeKind::LET_REGION,
                        {std::move(initializer), std::move(body)});
  out.regions.insert(out.regions.begin(),
                     LexicalRegion{0, 1, {{binder, type}}});
  return out;
}

Expr traverse(Expr sequence, Expr start, Expr state, Expr body,
              int element, int index, int accumulator, RType state_type) {
  Expr out = expression(NodeKind::TRAVERSE,
                        {std::move(sequence), std::move(start),
                         std::move(state), std::move(body)});
  out.regions.insert(out.regions.begin(), LexicalRegion{
      0, 3, {{element, RType::Int}, {index, RType::Int},
             {accumulator, state_type}}});
  out.traversals.insert(out.traversals.begin(),
                        TraversalSpec{0, TraversalDirection::Forward});
  return out;
}

Expr traverse_range(Expr sequence, Expr start, Expr begin, Expr end,
                    Expr state, Expr body, int element, int index,
                    int accumulator, RType state_type) {
  Expr out = expression(NodeKind::TRAVERSE_RANGE,
                        {std::move(sequence), std::move(start),
                         std::move(begin), std::move(end),
                         std::move(state), std::move(body)});
  out.regions.insert(out.regions.begin(), LexicalRegion{
      0, 5, {{element, RType::Int}, {index, RType::Int},
             {accumulator, state_type}}});
  out.traversals.insert(out.traversals.begin(),
                        TraversalSpec{0, TraversalDirection::Forward});
  return out;
}

AstProgram program(Expr root, std::vector<Value> constants,
                   std::vector<std::string> names = {}) {
  AstProgram ast;
  ast.consts = std::move(constants);
  ast.names = std::move(names);
  ast.nodes = {{NodeKind::PROGRAM, 0, 0}, {NodeKind::BLOCK_CONS, 0, 0},
               {NodeKind::RETURN, 0, 0}};
  for (auto& row : root.regions) row.node_index += 3;
  for (auto& row : root.traversals) row.node_index += 3;
  for (auto& row : root.fuel) row.node_index += 3;
  ast.nodes.insert(ast.nodes.end(), root.nodes.begin(), root.nodes.end());
  ast.nodes.push_back({NodeKind::BLOCK_NIL, 0, 0});
  ast.lexical_regions = std::move(root.regions);
  ast.traversal_specs = std::move(root.traversals);
  ast.fuel_specs = std::move(root.fuel);
  return ast;
}

ProgramGenome genome(const AstProgram& ast) {
  ProgramGenome out;
  out.ast = ast;
  out.meta = gagp::evo::build_genome_meta(ast);
  return out;
}

BytecodeProgram compile(const AstProgram& ast,
                        const std::vector<InputSpec>& inputs = {}) {
  const auto verified = gagp::evo::verify_ast(ast, inputs);
  if (!verified.ok)
    throw std::runtime_error("fuel optimization fixture failed verification: " +
                             verified.diagnostic.message);
  std::vector<std::string> names;
  for (const auto& input : inputs) names.push_back(input.name);
  return gagp::evo::compile_for_eval(genome(ast), verified.verified, names);
}

ExecResult run(const BytecodeProgram& bytecode, int fuel,
               const std::vector<std::pair<int, Value>>& inputs = {}) {
  return gagp::execute_bytecode_cpu(bytecode, inputs, fuel);
}

std::size_t opcode_count(const BytecodeProgram& bytecode, Opcode opcode) {
  return static_cast<std::size_t>(std::count_if(
      bytecode.code.begin(), bytecode.code.end(),
      [&](const auto& instruction) { return instruction.op == opcode; }));
}

std::size_t len_count(const BytecodeProgram& bytecode) {
  return static_cast<std::size_t>(std::count_if(
      bytecode.code.begin(), bytecode.code.end(), [](const auto& instruction) {
        return instruction.op == Opcode::CallBuiltin && instruction.has_a &&
               instruction.a == static_cast<int>(BuiltinId::Len);
      }));
}

int minimum_success(const BytecodeProgram& bytecode,
                    const std::vector<std::pair<int, Value>>& inputs = {}) {
  for (int fuel = 0; fuel <= 2000; ++fuel)
    if (!run(bytecode, fuel, inputs).is_error) return fuel;
  throw std::runtime_error("valid optimization fixture did not terminate");
}

bool exact_value(const Value& left, const Value& right) {
  if (left.tag != right.tag) return false;
  switch (left.tag) {
    case ValueTag::Int:
    case ValueTag::Char:
    case ValueTag::FallbackToken:
      return left.i == right.i;
    case ValueTag::Float:
      return left.f == right.f;
    case ValueTag::Bool:
      return left.b == right.b;
    case ValueTag::String: {
      std::string a;
      std::string b;
      return gagp::payload::lookup_string(left, &a) &&
             gagp::payload::lookup_string(right, &b) && a == b;
    }
    case ValueTag::IntList:
    case ValueTag::FloatList:
    case ValueTag::StringList: {
      std::vector<Value> a;
      std::vector<Value> b;
      if (!gagp::payload::lookup_list(left, &a) ||
          !gagp::payload::lookup_list(right, &b) || a.size() != b.size()) {
        return false;
      }
      for (std::size_t i = 0; i < a.size(); ++i)
        if (!exact_value(a[i], b[i])) return false;
      return true;
    }
    case ValueTag::Invalid:
      return true;
  }
  return false;
}

bool same_result(const ExecResult& left, const ExecResult& right) {
  if (left.is_error != right.is_error) return false;
  return left.is_error ? left.err.code == right.err.code
                       : exact_value(left.value, right.value);
}

int first_non_timeout(const BytecodeProgram& bytecode,
                      const std::vector<std::pair<int, Value>>& inputs = {}) {
  for (int fuel = 0; fuel <= 2000; ++fuel) {
    const ExecResult result = run(bytecode, fuel, inputs);
    if (!result.is_error || result.err.code != ErrCode::Timeout) return fuel;
  }
  throw std::runtime_error("optimization fixture has no finite fuel boundary");
}

bool same_at_every_fuel(
    const BytecodeProgram& optimized, const BytecodeProgram& blocked,
    const std::string& label,
    const std::vector<std::pair<int, Value>>& inputs = {}) {
  const int boundary = first_non_timeout(blocked, inputs);
  for (int fuel = 0; fuel <= boundary + 3; ++fuel) {
    if (!check(same_result(run(optimized, fuel, inputs),
                           run(blocked, fuel, inputs)),
               label + " differs at fuel " + std::to_string(fuel))) {
      return false;
    }
  }
  return true;
}

std::vector<Value> scalar_constants() {
  return {gagp::payload::make_int_list_value({}), Value::from_int(0),
          Value::from_int(1), Value::from_int(-1), Value::from_int(7)};
}

AstProgram simple_traversal(Expr start, std::vector<std::string> names = {}) {
  Expr root = traverse(leaf(NodeKind::CONST, 0), std::move(start),
                       leaf(NodeKind::CONST, 4), leaf(NodeKind::REGION_VAR, 3),
                       1, 2, 3, RType::Int);
  profile(&root, {{FuelEvent::CheckStart, 0}});
  return program(std::move(root), scalar_constants(), std::move(names));
}

bool test_proven_integer_initializers() {
  struct Case { Expr start; std::size_t checks; const char* name; };
  std::vector<Case> cases;
  cases.push_back({leaf(NodeKind::CONST, 1), 0, "Int constant"});
  cases.push_back({expression(NodeKind::CHECK_INT, {leaf(NodeKind::VAR, 0)}),
                   1, "explicit check"});
  cases.push_back({expression(NodeKind::CALL_LEN, {leaf(NodeKind::CONST, 0)}),
                   0, "length"});
  cases.push_back({expression(NodeKind::ADD,
                              {leaf(NodeKind::CONST, 2), leaf(NodeKind::CONST, 3)}),
                   0, "proven addition"});
  cases.push_back({expression(NodeKind::SUB,
                              {leaf(NodeKind::CONST, 2), leaf(NodeKind::CONST, 2)}),
                   0, "proven subtraction"});
  for (std::size_t i = 0; i < cases.size(); ++i) {
    const auto inputs = i == 1 ? std::vector<InputSpec>{{"x", RType::Int}} :
                                 std::vector<InputSpec>{};
    const auto runtime_inputs = i == 1 ?
        std::vector<std::pair<int, Value>>{{0, Value::from_int(0)}} :
        std::vector<std::pair<int, Value>>{};
    const auto bytecode = compile(simple_traversal(std::move(cases[i].start),
        i == 1 ? std::vector<std::string>{"x"} : std::vector<std::string>{}), inputs);
    if (!check(opcode_count(bytecode, Opcode::CheckInt) == cases[i].checks,
               std::string(cases[i].name) + " retained a redundant traversal check") ||
        !check(!run(bytecode, 200, runtime_inputs).is_error,
               std::string(cases[i].name) + " changed runtime behavior")) return false;
  }

  Expr nested = traverse(leaf(NodeKind::CONST, 0), leaf(NodeKind::REGION_VAR, 40),
      leaf(NodeKind::CONST, 4), leaf(NodeKind::REGION_VAR, 43), 41, 42, 43,
      RType::Int);
  profile(&nested, {{FuelEvent::CheckStart, 0}});
  const auto bytecode = compile(program(
      let(leaf(NodeKind::CONST, 1), std::move(nested), 40, RType::Int),
      scalar_constants()));
  if (!check(opcode_count(bytecode, Opcode::CheckInt) == 0,
             "lexically enclosing proven REGION_VAR retained a redundant check") ||
      !check(!run(bytecode, 200).is_error,
             "lexically enclosing integer fact changed runtime behavior")) return false;

  Expr ranged = traverse_range(
      leaf(NodeKind::CONST, 0),
      expression(NodeKind::CALL_LEN, {leaf(NodeKind::CONST, 0)}),
      expression(NodeKind::ADD,
                 {leaf(NodeKind::CONST, 2), leaf(NodeKind::CONST, 3)}),
      expression(NodeKind::SUB,
                 {leaf(NodeKind::CONST, 2), leaf(NodeKind::CONST, 2)}),
      leaf(NodeKind::CONST, 4), leaf(NodeKind::REGION_VAR, 73),
      71, 72, 73, RType::Int);
  profile(&ranged, {{FuelEvent::CheckStart, 0}, {FuelEvent::CheckBegin, 0},
                    {FuelEvent::CheckEnd, 0}});
  const auto ranged_bytecode = compile(program(std::move(ranged), scalar_constants()));
  return check(opcode_count(ranged_bytecode, Opcode::CheckInt) == 0,
               "proven range bounds retained redundant traversal checks") &&
         check(!run(ranged_bytecode, 200).is_error,
               "eliding proven range checks changed runtime behavior");
}

bool test_dynamic_input_check_and_error_precedence() {
  Expr root = traverse(leaf(NodeKind::CONST, 0), leaf(NodeKind::VAR, 0),
                       leaf(NodeKind::CONST, 4), leaf(NodeKind::REGION_VAR, 3),
                       1, 2, 3, RType::Int);
  profile(&root, {{FuelEvent::StoreSequence, 0}, {FuelEvent::StoreStart, 0},
                  {FuelEvent::CheckStart, 0}});
  root.fuel.push_back({1, {{FuelEvent::Operation, 0}}});
  root.fuel.push_back({2, {{FuelEvent::Operation, 0}}});
  const auto bytecode = compile(program(std::move(root), scalar_constants(), {"x"}),
                                {{"x", RType::Int}});
  return check(opcode_count(bytecode, Opcode::CheckInt) == 1,
               "static input type incorrectly proved a dynamic Int value") &&
         check(run(bytecode, 0, {{0, Value::from_bool(true)}}).is_error &&
                   run(bytecode, 0, {{0, Value::from_bool(true)}}).err.code == ErrCode::Type,
               "zero-cost dynamic check lost Type-over-timeout precedence");
}

AstProgram capture_alias_program(bool block_aliases, bool fallible_body = false) {
  Expr source = leaf(NodeKind::REGION_VAR, 40);
  profile(&source, {{FuelEvent::Operation, 0}});
  Expr start = leaf(NodeKind::REGION_VAR, 41);
  profile(&start, {{FuelEvent::Operation, 0}});
  if (block_aliases) {
    source = expression(NodeKind::CHECK_LIST, {std::move(source)});
    profile(&source, {{FuelEvent::Operation, 0}});
    start = expression(NodeKind::CHECK_INT, {std::move(start)});
    profile(&start, {{FuelEvent::Operation, 0}});
  }
  Expr body = fallible_body
      ? expression(NodeKind::MOD,
            {leaf(NodeKind::REGION_VAR, 42), leaf(NodeKind::CONST, 1)})
      : expression(NodeKind::ADD,
            {leaf(NodeKind::REGION_VAR, 44), leaf(NodeKind::REGION_VAR, 42)});
  Expr loop = traverse(std::move(source), std::move(start),
      leaf(NodeKind::CONST, 1), std::move(body), 42, 43, 44, RType::Int);
  profile(&loop, {{FuelEvent::StoreSequence, 0}, {FuelEvent::StoreStart, 0},
                  {FuelEvent::CheckStart, 0}});
  Expr root = let(leaf(NodeKind::CONST, 0),
      let(leaf(NodeKind::CONST, 1), std::move(loop), 41, RType::Int),
      40, RType::IntList);
  return program(std::move(root),
      {gagp::payload::make_int_list_value(
           {Value::from_int(2), Value::from_int(3)}),
       Value::from_int(0)});
}

bool test_zero_cost_capture_aliases_preserve_fuel() {
  const auto optimized = compile(capture_alias_program(false));
  const auto baseline = compile(capture_alias_program(true));
  const int boundary = minimum_success(baseline);
  const auto result = run(optimized, boundary);
  if (!check(optimized.code.size() < baseline.code.size(),
             "zero-cost sequence/start captures were not aliased") ||
      !check(minimum_success(optimized) == boundary,
             "capture aliasing changed the exact semantic fuel boundary") ||
      !check(!result.is_error && result.value.tag == ValueTag::Int &&
                 result.value.i == 5,
             "capture aliasing changed traversal behavior") ||
      !check(run(optimized, boundary - 1).is_error &&
                 run(optimized, boundary - 1).err.code == ErrCode::Timeout,
             "capture aliasing changed the timeout boundary")) {
    return false;
  }
  if (!same_at_every_fuel(optimized, baseline,
                          "capture alias optimization")) return false;

  const auto failing_optimized = compile(capture_alias_program(false, true));
  const auto failing_blocked = compile(capture_alias_program(true, true));
  return same_at_every_fuel(failing_optimized, failing_blocked,
                            "capture alias before fallible body") &&
         check(run(failing_optimized, first_non_timeout(failing_optimized)).is_error &&
                   run(failing_optimized, first_non_timeout(failing_optimized)).err.code ==
                       ErrCode::ZeroDiv,
               "fallible capture-alias fixture should reach ZeroDiv");
}

AstProgram clamp_program(Expr begin, Expr end, std::uint32_t clamp_cost,
                         std::vector<std::string> names = {}) {
  Expr root = traverse_range(
      leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1), std::move(begin),
      std::move(end), leaf(NodeKind::CONST, 4),
      leaf(NodeKind::REGION_VAR, 83), 81, 82, 83, RType::Int);
  profile(&root, {{FuelEvent::CheckStart, 0}, {FuelEvent::CheckBegin, 0},
                  {FuelEvent::CheckEnd, 0},
                  {FuelEvent::ClampBegin, clamp_cost},
                  {FuelEvent::ClampEnd, clamp_cost}});
  return program(std::move(root), scalar_constants(), std::move(names));
}

bool test_literal_zero_clamp_elision() {
  const auto zero = compile(clamp_program(
      leaf(NodeKind::CONST, 1), leaf(NodeKind::CONST, 1), 0));
  const auto charged = compile(clamp_program(
      leaf(NodeKind::CONST, 1), leaf(NodeKind::CONST, 1), 4));
  const auto nonzero = compile(clamp_program(
      leaf(NodeKind::CONST, 3), leaf(NodeKind::CONST, 2), 0));
  if (!check(opcode_count(charged, Opcode::Lt) ==
                 opcode_count(zero, Opcode::Lt) + 2 &&
                 opcode_count(charged, Opcode::Gt) ==
                 opcode_count(zero, Opcode::Gt) + 2,
             "charged literal-zero endpoint clamps were incorrectly removed") ||
      !check(opcode_count(nonzero, Opcode::Lt) ==
                 opcode_count(zero, Opcode::Lt) + 2 &&
                 opcode_count(nonzero, Opcode::Gt) ==
                 opcode_count(zero, Opcode::Gt) + 2,
             "zero-cost nonzero endpoint clamps were incorrectly removed")) return false;
  const int zero_boundary = minimum_success(zero);
  if (!check(minimum_success(charged) == zero_boundary + 8,
             "charged clamps did not retain their exact fuel cost") ||
      !check(!run(nonzero, 500).is_error && run(nonzero, 500).value.i == 7,
             "retained nonzero clamps changed range behavior")) return false;

  Expr blocked_zero = expression(NodeKind::CHECK_INT,
                                 {leaf(NodeKind::CONST, 1)});
  profile(&blocked_zero, {{FuelEvent::Operation, 0}});
  const auto blocked = compile(clamp_program(
      blocked_zero, blocked_zero, 0));
  if (!check(opcode_count(blocked, Opcode::Lt) ==
                 opcode_count(zero, Opcode::Lt) + 2,
             "zero-cost identity did not block literal clamp elision") ||
      !same_at_every_fuel(zero, blocked, "literal-zero clamp elision")) {
    return false;
  }

  const auto invalid = compile(clamp_program(
      leaf(NodeKind::VAR, 0), leaf(NodeKind::CONST, 1), 0, {"begin"}),
      {{"begin", RType::Int}});
  const auto error = run(invalid, 500, {{0, Value::from_bool(true)}});
  return check(opcode_count(invalid, Opcode::CheckInt) == 1,
               "dynamic endpoint check was incorrectly elided") &&
         check(opcode_count(invalid, Opcode::Lt) ==
                   opcode_count(zero, Opcode::Lt) + 1 &&
                   opcode_count(invalid, Opcode::Gt) ==
                   opcode_count(zero, Opcode::Gt) + 1,
               "dynamic endpoint clamp was incorrectly elided") &&
         check(error.is_error && error.err.code == ErrCode::Type,
               "invalid dynamic endpoint no longer reports Type");
}

enum class ReverseBody { SumElements, SumIndices, Fail };

AstProgram reverse_program(const std::vector<std::int64_t>& elements,
                           bool ranged, std::int64_t start,
                           std::int64_t begin, std::int64_t end,
                           ReverseBody body_kind,
                           std::uint32_t initialize_cursor,
                           std::uint32_t advance_cursor,
                           bool block_literal_begin = false) {
  std::vector<Value> values;
  values.reserve(elements.size());
  for (const auto value : elements) values.push_back(Value::from_int(value));
  std::vector<Value> constants{
      gagp::payload::make_int_list_value(values), Value::from_int(start),
      Value::from_int(begin), Value::from_int(end), Value::from_int(0),
      Value::from_int(0)};
  Expr body;
  if (body_kind == ReverseBody::SumElements) {
    body = expression(NodeKind::ADD,
        {leaf(NodeKind::REGION_VAR, 303),
         leaf(NodeKind::REGION_VAR, 301)});
  } else if (body_kind == ReverseBody::SumIndices) {
    body = expression(NodeKind::ADD,
        {leaf(NodeKind::REGION_VAR, 303),
         leaf(NodeKind::REGION_VAR, 302)});
  } else {
    body = expression(NodeKind::MOD,
        {leaf(NodeKind::REGION_VAR, 301), leaf(NodeKind::CONST, 5)});
  }

  Expr root;
  if (ranged) {
    Expr begin_expression = leaf(NodeKind::CONST, 2);
    if (block_literal_begin) {
      begin_expression = expression(NodeKind::CHECK_INT,
                                    {std::move(begin_expression)});
    }
    root = traverse_range(
        leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
        std::move(begin_expression), leaf(NodeKind::CONST, 3),
        leaf(NodeKind::CONST, 4), std::move(body), 301, 302, 303,
        RType::Int);
  } else {
    root = traverse(
        leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
        leaf(NodeKind::CONST, 4), std::move(body), 301, 302, 303,
        RType::Int);
  }
  root.traversals.at(0).direction = TraversalDirection::Reverse;
  std::vector<FuelCharge> charges{
      {FuelEvent::StoreSequence, 0}, {FuelEvent::StoreStart, 0},
      {FuelEvent::CheckStart, 0}, {FuelEvent::ObserveSequence, 0},
      {FuelEvent::InitializeState, 0},
      {FuelEvent::InitializeCursor, initialize_cursor},
      {FuelEvent::AdvanceCursor, advance_cursor},
  };
  if (ranged) {
    charges.insert(charges.end(), {
        {FuelEvent::StoreBegin, 0}, {FuelEvent::StoreEnd, 0},
        {FuelEvent::CheckBegin, 0}, {FuelEvent::CheckEnd, 0},
        {FuelEvent::ClampBegin, 0}, {FuelEvent::ClampEnd, 0}});
  } else {
    charges.push_back({FuelEvent::SetBegin, 0});
    charges.push_back({FuelEvent::SetEnd, 0});
  }
  root.fuel.insert(root.fuel.begin(), NodeFuelSpec{0, std::move(charges)});
  for (std::size_t i = 1; i < root.nodes.size(); ++i) {
    if (gagp::evo::supports_fuel_event(root.nodes[i].kind,
                                       FuelEvent::Operation)) {
      root.fuel.push_back({i, {{FuelEvent::Operation, 0}}});
    }
  }
  return program(std::move(root), std::move(constants));
}

bool reverse_shifted_at_every_fuel(const AstProgram& source,
                                   const std::string& label,
                                   const Value* expected = nullptr,
                                   ErrCode expected_error = ErrCode::Value) {
  const auto optimized = compile(source);
  AstProgram reference = source;
  for (auto& charge : reference.fuel_specs.front().charges) {
    if (charge.event == FuelEvent::InitializeCursor) charge.cost = 1;
  }
  const auto blocked = compile(reference);
  const int boundary = first_non_timeout(optimized);
  if (!check(first_non_timeout(blocked) == boundary + 1,
             label + ": reference did not add exactly one initialization charge")) {
    return false;
  }
  for (int fuel = 0; fuel <= boundary + 3; ++fuel) {
    if (!check(same_result(run(optimized, fuel), run(blocked, fuel + 1)),
               label + ": shifted result differs at fuel " +
                   std::to_string(fuel))) return false;
  }
  const auto result = run(optimized, boundary);
  if (expected != nullptr) {
    return check(!result.is_error && exact_value(result.value, *expected),
                 label + ": optimized traversal returned the wrong value");
  }
  return check(result.is_error && result.err.code == expected_error,
               label + ": optimized traversal returned the wrong error");
}

bool test_reverse_zero_cursor_optimization() {
  const Value zero = Value::from_int(0);
  const Value four = Value::from_int(4);
  const Value ten = Value::from_int(10);
  const Value eight = Value::from_int(8);
  const Value indices = Value::from_int(33);
  if (!reverse_shifted_at_every_fuel(
          reverse_program({}, false, 0, 0, 0, ReverseBody::SumElements, 0, 0),
          "reverse empty", &zero) ||
      !reverse_shifted_at_every_fuel(
          reverse_program({4}, false, 0, 0, 0,
                          ReverseBody::SumElements, 0, 0),
          "reverse singleton", &four) ||
      !reverse_shifted_at_every_fuel(
          reverse_program({2, 3, 5}, false, 0, 0, 0,
                          ReverseBody::SumElements, 0, 0),
          "reverse multiple", &ten) ||
      !reverse_shifted_at_every_fuel(
          reverse_program({2, 3, 5, 7}, true, 0, 1, 3,
                          ReverseBody::SumElements, 0, 0),
          "reverse range", &eight) ||
      !reverse_shifted_at_every_fuel(
          reverse_program({2, 3, 5}, false, 10, 0, 0,
                          ReverseBody::SumIndices, 0, 0),
          "reverse semantic start index", &indices) ||
      !reverse_shifted_at_every_fuel(
          reverse_program({2}, false, 0, 0, 0,
                          ReverseBody::Fail, 0, 0),
          "reverse fallible step", nullptr, ErrCode::ZeroDiv)) {
    return false;
  }

  const auto fast = compile(reverse_program(
      {2, 3}, false, 0, 0, 0, ReverseBody::SumElements, 0, 0));
  const auto charged_init = compile(reverse_program(
      {2, 3}, false, 0, 0, 0, ReverseBody::SumElements, 1, 0));
  const auto charged_advance = compile(reverse_program(
      {2, 3}, false, 0, 0, 0, ReverseBody::SumElements, 0, 1));
  return check(opcode_count(fast, Opcode::Sub) + 1 ==
                   opcode_count(charged_init, Opcode::Sub) &&
                   opcode_count(fast, Opcode::Sub) + 1 ==
                   opcode_count(charged_advance, Opcode::Sub),
               "charged cursor events did not retain the reverse reference path");
}

bool test_literal_zero_begin_storage_elision() {
  const auto literal_ast = reverse_program(
      {2, 3, 5}, true, 0, 0, 3, ReverseBody::SumElements, 0, 0);
  const auto blocked_ast = reverse_program(
      {2, 3, 5}, true, 0, 0, 3, ReverseBody::SumElements, 0, 0, true);
  const auto literal = compile(literal_ast);
  const auto blocked = compile(blocked_ast);
  return check(literal.code.size() < blocked.code.size(),
               "literal-zero begin storage was not removed") &&
         same_at_every_fuel(literal, blocked,
                            "literal-zero begin storage elision");
}

Expr zero_cost_check(Expr value) {
  Expr checked = expression(NodeKind::CHECK_INT, {std::move(value)});
  profile(&checked, {{FuelEvent::Operation, 0}});
  return checked;
}

Expr length_endpoint(int length_binder, int constant_index, bool subtract,
                     bool blocked) {
  Expr endpoint = constant_index < 0
      ? leaf(NodeKind::REGION_VAR, length_binder)
      : expression(subtract ? NodeKind::SUB : NodeKind::ADD,
            {leaf(NodeKind::REGION_VAR, length_binder),
             leaf(NodeKind::CONST, constant_index)});
  return blocked ? zero_cost_check(std::move(endpoint)) : endpoint;
}

Expr symbolic_range(int source_binder, Expr endpoint,
                    std::uint32_t clamp_cost = 0, int binder_base = 91) {
  Expr loop = traverse_range(
      leaf(NodeKind::REGION_VAR, source_binder), leaf(NodeKind::CONST, 1),
      leaf(NodeKind::CONST, 1), std::move(endpoint),
      leaf(NodeKind::CONST, 3),
      leaf(NodeKind::REGION_VAR, binder_base + 2),
      binder_base, binder_base + 1, binder_base + 2, RType::Int);
  profile(&loop, {{FuelEvent::CheckStart, 0}, {FuelEvent::CheckBegin, 0},
                  {FuelEvent::CheckEnd, 0},
                  {FuelEvent::ObserveSequence, 0},
                  {FuelEvent::ClampBegin, 0},
                  {FuelEvent::ClampEnd, clamp_cost}});
  return loop;
}

AstProgram symbolic_length_program(const Value& source, int constant_index,
                                   bool subtract, bool blocked,
                                   bool alias_length = false,
                                   std::uint32_t clamp_cost = 0) {
  constexpr int kSource = 80;
  constexpr int kLength = 81;
  constexpr int kAlias = 82;
  const int endpoint_binder = alias_length ? kAlias : kLength;
  Expr loop = symbolic_range(
      kSource,
      length_endpoint(endpoint_binder, constant_index, subtract, blocked),
      clamp_cost);
  if (alias_length) {
    loop = let(leaf(NodeKind::REGION_VAR, kLength), std::move(loop),
               kAlias, RType::Int);
  }
  Expr root = let(
      leaf(NodeKind::CONST, 0),
      let(expression(NodeKind::CALL_LEN,
                     {leaf(NodeKind::REGION_VAR, kSource)}),
          std::move(loop), kLength, RType::Int),
      kSource, RType::IntList);
  return program(std::move(root),
                 {source, Value::from_int(0), Value::from_int(1),
                  Value::from_int(7), Value::from_int(-1),
                  Value::from_int(std::numeric_limits<std::int64_t>::max()),
                  Value::from_int(-2)});
}

bool compare_symbolic_length(const Value& source, int constant_index,
                             bool subtract, const std::string& label,
                             bool alias_length = false,
                             std::uint32_t clamp_cost = 0,
                             int removed_lt = 0, int removed_gt = 0) {
  const auto optimized = compile(symbolic_length_program(
      source, constant_index, subtract, false, alias_length, clamp_cost));
  const auto blocked = compile(symbolic_length_program(
      source, constant_index, subtract, true, alias_length, clamp_cost));
  return check(opcode_count(optimized, Opcode::Lt) +
                       static_cast<std::size_t>(removed_lt) ==
                   opcode_count(blocked, Opcode::Lt),
               label + " removed the wrong lower clamp half") &&
         check(opcode_count(optimized, Opcode::Gt) +
                       static_cast<std::size_t>(removed_gt) ==
                   opcode_count(blocked, Opcode::Gt),
               label + " removed the wrong upper clamp half") &&
         same_at_every_fuel(optimized, blocked, label);
}

bool test_symbolic_length_endpoint_clamps() {
  const std::vector<std::pair<Value, const char*>> sources{
      {gagp::payload::make_int_list_value({}), "empty"},
      {gagp::payload::make_int_list_value({Value::from_int(4)}), "singleton"},
      {gagp::payload::make_int_list_value(
           {Value::from_int(4), Value::from_int(5), Value::from_int(6)}),
       "multiple"},
  };
  for (const auto& item : sources) {
    if (!compare_symbolic_length(item.first, -1, false,
                                 std::string(item.second) + " length endpoint",
                                 false, 0, 1, 1) ||
        !compare_symbolic_length(item.first, 2, true,
                                 std::string(item.second) + " length-1 endpoint",
                                 false, 0, 0, 1) ||
        !compare_symbolic_length(item.first, 4, false,
                                 std::string(item.second) + " length+(-1) endpoint",
                                 false, 0, 0, 1) ||
        !compare_symbolic_length(item.first, 6, false,
                                 std::string(item.second) + " length+(-2) endpoint",
                                 false, 0, 0, 1) ||
        !compare_symbolic_length(item.first, -1, false,
                                 std::string(item.second) + " aliased length endpoint",
                                 true, 0, 1, 1)) {
      return false;
    }
  }
  return true;
}

AstProgram guarded_length_program(const Value& source, bool equality,
                                  bool blocked, bool traversal_in_both) {
  constexpr int kSource = 100;
  constexpr int kLength = 101;
  Expr selected = symbolic_range(
      kSource, length_endpoint(kLength, 2, true, blocked), 0, 130);
  Expr other = traversal_in_both
      ? symbolic_range(kSource,
            length_endpoint(kLength, 2, true, blocked), 0, 140)
      : leaf(NodeKind::CONST, 3);
  Expr condition = expression(equality ? NodeKind::EQ : NodeKind::NE,
      {leaf(NodeKind::REGION_VAR, kLength), leaf(NodeKind::CONST, 1)});
  Expr branches = equality
      ? expression(NodeKind::IF_EXPR,
            {std::move(condition), std::move(other), std::move(selected)})
      : expression(NodeKind::IF_EXPR,
            {std::move(condition), std::move(selected), std::move(other)});
  Expr root = let(
      leaf(NodeKind::CONST, 0),
      let(expression(NodeKind::CALL_LEN,
                     {leaf(NodeKind::REGION_VAR, kSource)}),
          std::move(branches), kLength, RType::Int),
      kSource, RType::IntList);
  return program(std::move(root),
                 {source, Value::from_int(0), Value::from_int(1),
                  Value::from_int(7)});
}

bool test_nonempty_branch_length_minus_one() {
  const std::vector<std::pair<Value, const char*>> sources{
      {gagp::payload::make_int_list_value({}), "empty"},
      {gagp::payload::make_int_list_value({Value::from_int(2)}), "singleton"},
      {gagp::payload::make_int_list_value(
           {Value::from_int(2), Value::from_int(3), Value::from_int(5)}),
       "multiple"},
  };
  for (bool equality : {false, true}) {
    for (const auto& item : sources) {
      const std::string label = std::string(equality ? "EQ " : "NE ") +
                                item.second + " nonempty branch";
      const auto optimized =
          compile(guarded_length_program(item.first, equality, false, false));
      const auto blocked =
          compile(guarded_length_program(item.first, equality, true, false));
      if (!check(opcode_count(optimized, Opcode::Lt) + 1 ==
                     opcode_count(blocked, Opcode::Lt) &&
                     opcode_count(optimized, Opcode::Gt) + 1 ==
                     opcode_count(blocked, Opcode::Gt),
                 label + " did not remove the complete endpoint clamp") ||
          !same_at_every_fuel(optimized, blocked, label)) {
        return false;
      }
    }
  }

  // Both branches contain the same shape. Only the branch known nonempty may
  // discard the lower half; the zero-length sibling must retain it.
  const Value multiple = gagp::payload::make_int_list_value(
      {Value::from_int(1), Value::from_int(2)});
  const auto optimized =
      compile(guarded_length_program(multiple, false, false, true));
  const auto blocked =
      compile(guarded_length_program(multiple, false, true, true));
  return check(opcode_count(optimized, Opcode::Lt) + 1 ==
                   opcode_count(blocked, Opcode::Lt) &&
                   opcode_count(optimized, Opcode::Gt) + 2 ==
                   opcode_count(blocked, Opcode::Gt),
               "nonempty length fact leaked into the sibling branch") &&
         same_at_every_fuel(optimized, blocked,
                            "sibling branch length fact restoration");
}

Expr zero_cost_ref(int binder) {
  Expr result = leaf(NodeKind::REGION_VAR, binder);
  profile(&result, {{FuelEvent::Operation, 0}});
  return result;
}

AstProgram shared_start_end_program(const Value& source, bool blocked_end) {
  constexpr int kSource = 150;
  constexpr int kLength = 151;
  Expr end = zero_cost_ref(kLength);
  if (blocked_end) end = zero_cost_check(std::move(end));
  Expr loop = traverse_range(
      leaf(NodeKind::REGION_VAR, kSource), zero_cost_ref(kLength),
      leaf(NodeKind::CONST, 1), std::move(end),
      leaf(NodeKind::CONST, 2), leaf(NodeKind::REGION_VAR, 163),
      161, 162, 163, RType::Int);
  profile(&loop, {{FuelEvent::StoreStart, 0}, {FuelEvent::StoreEnd, 0},
                  {FuelEvent::CheckStart, 2}, {FuelEvent::CheckBegin, 0},
                  {FuelEvent::CheckEnd, 5},
                  {FuelEvent::ObserveSequence, 0},
                  {FuelEvent::ClampBegin, 0}, {FuelEvent::ClampEnd, 0}});
  Expr root = let(
      leaf(NodeKind::CONST, 0),
      let(expression(NodeKind::CALL_LEN,
                     {leaf(NodeKind::REGION_VAR, kSource)}),
          std::move(loop), kLength, RType::Int),
      kSource, RType::IntList);
  return program(std::move(root),
                 {source, Value::from_int(0), Value::from_int(7)});
}

std::vector<std::uint32_t> check_anchor_costs(
    const BytecodeProgram& bytecode) {
  std::vector<std::uint32_t> result;
  for (std::size_t i = 1; i < bytecode.code.size(); ++i) {
    if (bytecode.code[i].op == Opcode::CheckInt) {
      result.push_back(bytecode.instruction_fuel[i - 1]);
    }
  }
  return result;
}

bool test_shared_start_end_alias_keeps_event_roles() {
  const std::vector<Value> sources{
      gagp::payload::make_int_list_value({}),
      gagp::payload::make_int_list_value({Value::from_int(4)}),
      gagp::payload::make_int_list_value(
          {Value::from_int(4), Value::from_int(5), Value::from_int(6)}),
  };
  for (std::size_t i = 0; i < sources.size(); ++i) {
    const auto optimized = compile(shared_start_end_program(sources[i], false));
    const auto blocked = compile(shared_start_end_program(sources[i], true));
    const auto anchors = check_anchor_costs(optimized);
    if (!check(anchors == std::vector<std::uint32_t>{2, 5},
               "shared start/end slot confused CheckStart and CheckEnd roles") ||
        !check(optimized.code.size() < blocked.code.size(),
               "identity end capture was not aliased") ||
        !same_at_every_fuel(
            optimized, blocked,
            "shared start/end alias case " + std::to_string(i))) {
      return false;
    }
  }
  return true;
}

AstProgram other_source_length_program(bool blocked) {
  constexpr int kSource = 110;
  constexpr int kOther = 111;
  constexpr int kLength = 112;
  Expr loop = symbolic_range(
      kOther, length_endpoint(kLength, -1, false, blocked));
  Expr root = let(
      leaf(NodeKind::CONST, 0),
      let(expression(NodeKind::CALL_LEN,
                     {leaf(NodeKind::REGION_VAR, kSource)}),
          let(leaf(NodeKind::CONST, 4), std::move(loop), kOther,
              RType::IntList),
          kLength, RType::Int),
      kSource, RType::IntList);
  return program(std::move(root),
      {gagp::payload::make_int_list_value(
           {Value::from_int(1), Value::from_int(2), Value::from_int(3)}),
       Value::from_int(0), Value::from_int(1), Value::from_int(7),
       gagp::payload::make_int_list_value({Value::from_int(9)})});
}

bool test_symbolic_length_safety_guards() {
  const auto other_optimized = compile(other_source_length_program(false));
  const auto other_blocked = compile(other_source_length_program(true));
  if (!check(opcode_count(other_optimized, Opcode::Lt) ==
                 opcode_count(other_blocked, Opcode::Lt) &&
                 opcode_count(other_optimized, Opcode::Gt) ==
                 opcode_count(other_blocked, Opcode::Gt),
             "length relation crossed to a different sequence source") ||
      !same_at_every_fuel(other_optimized, other_blocked,
                          "different sequence source")) {
    return false;
  }

  const Value xs = gagp::payload::make_int_list_value(
      {Value::from_int(1), Value::from_int(2)});
  if (!compare_symbolic_length(xs, -1, false, "charged symbolic clamp",
                               false, 4, 0, 0) ||
      !compare_symbolic_length(xs, 5, false, "large literal offset",
                               false, 0, 0, 0)) {
    return false;
  }

  constexpr int kSource = 120;
  constexpr int kLength = 121;
  const auto dynamic_program = [&](bool blocked) {
    Expr endpoint = expression(NodeKind::ADD,
        {leaf(NodeKind::REGION_VAR, kLength), leaf(NodeKind::VAR, 0)});
    if (blocked) endpoint = zero_cost_check(std::move(endpoint));
    Expr loop = symbolic_range(kSource, std::move(endpoint));
    Expr root = let(
        leaf(NodeKind::CONST, 0),
        let(expression(NodeKind::CALL_LEN,
                       {leaf(NodeKind::REGION_VAR, kSource)}),
            std::move(loop), kLength, RType::Int),
        kSource, RType::IntList);
    return program(
        std::move(root),
        {xs, Value::from_int(0), Value::from_int(1), Value::from_int(7)},
        {"offset"});
  };
  const std::vector<InputSpec> input_types{{"offset", RType::Int}};
  const auto bytecode = compile(dynamic_program(false), input_types);
  const auto blocked_dynamic = compile(dynamic_program(true), input_types);
  const auto runtime_inputs =
      std::vector<std::pair<int, Value>>{{0, Value::from_bool(true)}};
  const ExecResult error = run(bytecode, 500, runtime_inputs);
  return check(opcode_count(bytecode, Opcode::CheckInt) >= 1,
               "dynamic symbolic offset lost its runtime type check") &&
         same_at_every_fuel(bytecode, blocked_dynamic,
                            "dynamic symbolic offset", runtime_inputs) &&
         check(error.is_error && error.err.code == ErrCode::Type,
               "dynamic symbolic offset no longer reports Type");
}

Expr increment_traversal(int source_binder, bool block_length_alias) {
  Expr source = leaf(NodeKind::REGION_VAR, source_binder);
  if (block_length_alias) {
    source = expression(NodeKind::CHECK_LIST, {std::move(source)});
    profile(&source, {{FuelEvent::Operation, 0}});
  }
  Expr body = expression(NodeKind::ADD,
      {leaf(NodeKind::REGION_VAR, 53), leaf(NodeKind::CONST, 2)});
  Expr traversal = traverse(std::move(source), leaf(NodeKind::CONST, 1),
      leaf(NodeKind::CONST, 1), std::move(body), 51, 52, 53, RType::Int);
  profile(&traversal, {{FuelEvent::ObserveSequence, 0}});
  return traversal;
}

AstProgram sibling_scope_program(bool block_length_alias) {
  Expr then_branch = let(
      expression(NodeKind::CALL_LEN, {leaf(NodeKind::REGION_VAR, 40)}),
      leaf(NodeKind::CONST, 1), 41, RType::Int);
  Expr root = let(leaf(NodeKind::VAR, 0),
      expression(NodeKind::IF_EXPR,
          {leaf(NodeKind::CONST, 5), std::move(then_branch),
           increment_traversal(40, block_length_alias)}), 40, RType::IntList);
  auto constants = scalar_constants();
  constants.push_back(Value::from_bool(false));
  return program(std::move(root), std::move(constants), {"xs"});
}

bool test_length_cache_scope_does_not_leak_to_sibling() {
  const auto bytecode = compile(sibling_scope_program(false),
                                {{"xs", RType::IntList}});
  const auto baseline = compile(sibling_scope_program(true),
                                {{"xs", RType::IntList}});
  const auto xs = gagp::payload::make_int_list_value(
      {Value::from_int(4), Value::from_int(5), Value::from_int(6)});
  const auto result = run(bytecode, 500, {{0, xs}});
  return check(len_count(bytecode) == len_count(baseline),
               "length fact leaked from one sibling into another") &&
         check(minimum_success(bytecode, {{0, xs}}) ==
                   minimum_success(baseline, {{0, xs}}),
               "sibling-scoped length fact changed the fuel boundary") &&
         check(!result.is_error && result.value.tag == ValueTag::Int && result.value.i == 3,
               "length fact leaked from an unexecuted sibling let scope");
}

Expr doubling_inner(int state_binder, bool block_length_alias,
                    bool fallible_body = false) {
  Expr source = leaf(NodeKind::REGION_VAR, state_binder);
  if (block_length_alias) {
    source = expression(NodeKind::CHECK_LIST, {std::move(source)});
    profile(&source, {{FuelEvent::Operation, 0}});
  }
  Expr step = fallible_body
      ? expression(NodeKind::CHECK_LIST, {leaf(NodeKind::VAR, 0)})
      : expression(NodeKind::CALL_APPEND,
            {leaf(NodeKind::REGION_VAR, 67), leaf(NodeKind::REGION_VAR, 65)});
  Expr inner = traverse(std::move(source), leaf(NodeKind::CONST, 1),
      leaf(NodeKind::REGION_VAR, state_binder), std::move(step),
      65, 66, 67, RType::IntList);
  profile(&inner, {{FuelEvent::ObserveSequence, 0}});
  return let(expression(NodeKind::CALL_LEN, {leaf(NodeKind::REGION_VAR, state_binder)}),
             std::move(inner), 64, RType::Int);
}

AstProgram nested_traversals(bool block_length_alias,
                             bool fallible_body = false) {
  Expr outer = traverse(leaf(NodeKind::CONST, 6), leaf(NodeKind::CONST, 1),
      leaf(NodeKind::CONST, 7),
      doubling_inner(63, block_length_alias, fallible_body),
      61, 62, 63, RType::IntList);
  auto constants = scalar_constants();
  constants.push_back(Value::from_bool(false));
  constants.push_back(gagp::payload::make_int_list_value(
      {Value::from_int(2), Value::from_int(3)}));
  constants.push_back(gagp::payload::make_int_list_value({Value::from_int(1)}));
  return program(std::move(outer), std::move(constants),
                 fallible_body ? std::vector<std::string>{"bad"}
                               : std::vector<std::string>{});
}

bool test_nested_state_length_cache_is_fresh_each_iteration() {
  const auto optimized = compile(nested_traversals(false));
  const auto baseline = compile(nested_traversals(true));
  if (!check(len_count(optimized) + 1 == len_count(baseline),
             "dominating let-bound length was not reused exactly once") ||
      !check(minimum_success(optimized) == minimum_success(baseline),
             "length reuse changed the exact semantic fuel boundary")) return false;
  if (!same_at_every_fuel(optimized, baseline,
                          "dominating length reuse")) return false;
  const auto result = run(optimized, 2000);
  std::vector<Value> values;
  if (!check(!result.is_error && result.value.tag == ValueTag::IntList &&
                   gagp::payload::lookup_list(result.value, &values) && values.size() == 4,
               "nested traversal reused stale state length across iterations")) {
    return false;
  }

  const std::vector<InputSpec> input_types{{"bad", RType::IntList}};
  const std::vector<std::pair<int, Value>> runtime_inputs{
      {0, Value::from_bool(true)}};
  const auto failing_optimized =
      compile(nested_traversals(false, true), input_types);
  const auto failing_blocked =
      compile(nested_traversals(true, true), input_types);
  return same_at_every_fuel(failing_optimized, failing_blocked,
                            "length reuse before fallible child",
                            runtime_inputs) &&
         check(run(failing_optimized,
                   first_non_timeout(failing_optimized, runtime_inputs),
                   runtime_inputs).is_error &&
                   run(failing_optimized,
                       first_non_timeout(failing_optimized, runtime_inputs),
                       runtime_inputs).err.code == ErrCode::Type,
               "fallible length-reuse fixture should reach Type");
}
}  // namespace

int main() {
  gagp::payload::clear();
  if (!test_proven_integer_initializers()) return 1;
  if (!test_dynamic_input_check_and_error_precedence()) return 1;
  if (!test_zero_cost_capture_aliases_preserve_fuel()) return 1;
  if (!test_literal_zero_clamp_elision()) return 1;
  if (!test_reverse_zero_cursor_optimization()) return 1;
  if (!test_literal_zero_begin_storage_elision()) return 1;
  if (!test_symbolic_length_endpoint_clamps()) return 1;
  if (!test_nonempty_branch_length_minus_one()) return 1;
  if (!test_shared_start_end_alias_keeps_event_roles()) return 1;
  if (!test_symbolic_length_safety_guards()) return 1;
  if (!test_length_cache_scope_does_not_leak_to_sibling()) return 1;
  if (!test_nested_state_length_cache_is_fresh_each_iteration()) return 1;
  std::cout << "gagp_test_fuel_optimization: OK\n";
  return 0;
}
