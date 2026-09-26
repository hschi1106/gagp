#include <cstdint>
#include <iostream>
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
using gagp::evo::VerifyCode;

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

ProgramGenome genome(const AstProgram& ast) {
  ProgramGenome result;
  result.ast = ast;
  result.meta = gagp::evo::build_genome_meta(ast);
  return result;
}

BytecodeProgram compile(const AstProgram& ast,
                        const std::vector<InputSpec>& inputs = {}) {
  const auto verified = gagp::evo::verify_ast(ast, inputs);
  if (!verified.ok) {
    throw std::runtime_error(std::string("fixture verification failed: ") +
                             gagp::evo::verify_code_name(verified.diagnostic.code) +
                             " " + verified.diagnostic.message);
  }
  std::vector<std::string> names;
  for (const auto& input : inputs) names.push_back(input.name);
  return gagp::evo::compile_for_eval(genome(ast), verified.verified, names);
}

ExecResult run(const BytecodeProgram& program, int fuel,
               const std::vector<std::pair<int, Value>>& inputs = {}) {
  return gagp::execute_bytecode_cpu(program, inputs, fuel);
}

bool is_int(const ExecResult& result, std::int64_t expected) {
  return !result.is_error && result.value.tag == ValueTag::Int &&
         result.value.i == expected;
}

bool is_error(const ExecResult& result, ErrCode expected) {
  return result.is_error && result.err.code == expected;
}

void operation(AstProgram* ast, std::size_t node, std::uint32_t cost) {
  ast->fuel_specs.push_back({node, {{FuelEvent::Operation, cost}}});
}

std::vector<FuelCharge> traversal_charges(std::uint32_t cost, bool ranged) {
  std::vector<FuelCharge> out{
      {FuelEvent::StoreSequence, cost}, {FuelEvent::StoreStart, cost},
      {FuelEvent::CheckStart, cost}, {FuelEvent::ObserveSequence, cost},
      {FuelEvent::InitializeState, cost}, {FuelEvent::InitializeCursor, cost},
      {FuelEvent::TestCursor, cost}, {FuelEvent::ReadElement, cost},
      {FuelEvent::BindElement, cost}, {FuelEvent::ComputeIndex, cost},
      {FuelEvent::UpdateState, cost}, {FuelEvent::AdvanceCursor, cost},
      {FuelEvent::Repeat, cost}, {FuelEvent::Result, cost},
  };
  if (ranged) {
    out.push_back({FuelEvent::StoreBegin, cost});
    out.push_back({FuelEvent::StoreEnd, cost});
    out.push_back({FuelEvent::CheckBegin, cost});
    out.push_back({FuelEvent::CheckEnd, cost});
    out.push_back({FuelEvent::ClampBegin, cost});
    out.push_back({FuelEvent::ClampEnd, cost});
  } else {
    out.push_back({FuelEvent::SetBegin, cost});
    out.push_back({FuelEvent::SetEnd, cost});
  }
  return out;
}

void add_traversal_metadata(AstProgram* ast, TraversalDirection direction,
                            int element, int index, int state,
                            int body_argument) {
  ast->lexical_regions.push_back(
      LexicalRegion{3, body_argument,
                    {{element, RType::Int}, {index, RType::Int},
                     {state, RType::Int}}});
  ast->traversal_specs.push_back(TraversalSpec{3, direction});
}

AstProgram traversal(bool ranged, TraversalDirection direction,
                     std::uint32_t phase_cost, bool profile_children) {
  constexpr int kElement = 101;
  constexpr int kIndex = 102;
  constexpr int kState = 103;
  const Expr step = binary(NodeKind::ADD, leaf(NodeKind::REGION_VAR, kState),
                           leaf(NodeKind::REGION_VAR, kElement));
  const Value xs = gagp::payload::make_int_list_value(
      {Value::from_int(2), Value::from_int(3), Value::from_int(5)});
  Expr root;
  std::vector<Value> constants;
  if (ranged) {
    root = expression(NodeKind::TRAVERSE_RANGE,
                      {leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
                       leaf(NodeKind::CONST, 2), leaf(NodeKind::CONST, 3),
                       leaf(NodeKind::CONST, 4), step});
    constants = {xs, Value::from_int(0), Value::from_int(1),
                 Value::from_int(3), Value::from_int(0)};
  } else {
    root = expression(NodeKind::TRAVERSE,
                      {leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
                       leaf(NodeKind::CONST, 2), step});
    constants = {xs, Value::from_int(0), Value::from_int(0)};
  }
  AstProgram ast = return_program(root, std::move(constants));
  add_traversal_metadata(&ast, direction, kElement, kIndex, kState,
                         ranged ? 5 : 3);
  ast.fuel_specs.push_back({3, traversal_charges(phase_cost, ranged)});
  if (profile_children) {
    for (std::size_t i = 4; i + 1 < ast.nodes.size(); ++i) {
      if (ast.nodes[i].kind == NodeKind::CONST ||
          ast.nodes[i].kind == NodeKind::REGION_VAR ||
          ast.nodes[i].kind == NodeKind::ADD) {
        operation(&ast, i, 0);
      }
    }
  }
  return ast;
}

bool test_let_and_arithmetic_exact_boundary() {
  // let x = 2 in x + 3. RETURN is deliberately the remaining unit charge.
  AstProgram ast = return_program(
      expression(NodeKind::LET_REGION,
                 {leaf(NodeKind::CONST, 0),
                  binary(NodeKind::ADD, leaf(NodeKind::REGION_VAR, 40),
                         leaf(NodeKind::CONST, 1))}),
      {Value::from_int(2), Value::from_int(3)});
  ast.lexical_regions.push_back(
      LexicalRegion{3, 1, {{40, RType::Int}}});
  ast.fuel_specs.push_back({3, {{FuelEvent::Bind, 5}}});
  operation(&ast, 4, 2);
  operation(&ast, 5, 7);
  operation(&ast, 6, 3);
  operation(&ast, 7, 4);
  const BytecodeProgram bytecode = compile(ast);
  constexpr int kMinimum = 2 + 5 + 3 + 4 + 7 + 1;
  return check(is_int(run(bytecode, kMinimum), 5),
               "let/arithmetic should succeed at its semantic minimum") &&
         check(is_error(run(bytecode, kMinimum - 1), ErrCode::Timeout),
               "let/arithmetic should time out one unit below its minimum");
}

bool test_lazy_branches_and_error_precedence() {
  AstProgram ast = return_program(
      expression(NodeKind::IF_EXPR,
                 {leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
                  leaf(NodeKind::CONST, 2)}),
      {Value::from_bool(true), Value::from_int(7), Value::from_int(9)});
  ast.fuel_specs.push_back(
      {3, {{FuelEvent::BranchTest, 3}, {FuelEvent::BranchMerge, 7}}});
  operation(&ast, 4, 2);
  operation(&ast, 5, 5);
  operation(&ast, 6, 11);
  BytecodeProgram bytecode = compile(ast);
  if (!check(is_int(run(bytecode, 18), 7),
             "then path should charge condition, test, chosen leaf, merge, return") ||
      !check(is_error(run(bytecode, 17), ErrCode::Timeout),
             "then path should require its merge charge")) {
    return false;
  }
  ast.consts[0] = Value::from_bool(false);
  bytecode = compile(ast);
  if (!check(is_int(run(bytecode, 17), 9),
             "else path should skip both the then leaf and branch merge") ||
      !check(is_error(run(bytecode, 16), ErrCode::Timeout),
             "else path should enforce only its selected costs")) {
    return false;
  }

  AstProgram divide = return_program(
      binary(NodeKind::DIV, leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1)),
      {Value::from_int(1), Value::from_int(0)});
  operation(&divide, 3, 4);
  operation(&divide, 4, 0);
  operation(&divide, 5, 0);
  const BytecodeProgram fallible = compile(divide);
  if (!check(is_error(run(fallible, 3), ErrCode::Timeout),
             "fuel charge should precede division-by-zero") ||
      !check(is_error(run(fallible, 4), ErrCode::ZeroDiv),
             "paid operation should report division-by-zero")) {
    return false;
  }

  AstProgram typed = return_program(
      binary(NodeKind::ADD, leaf(NodeKind::VAR, 0), leaf(NodeKind::CONST, 0)),
      {Value::from_int(1)}, {"x"});
  operation(&typed, 3, 4);
  operation(&typed, 4, 0);
  operation(&typed, 5, 0);
  const BytecodeProgram type_fallible = compile(typed, {{"x", RType::Int}});
  return check(is_error(run(type_fallible, 3, {{0, Value::from_bool(true)}}),
                        ErrCode::Timeout),
               "fuel charge should precede an arithmetic type error") &&
         check(is_error(run(type_fallible, 4, {{0, Value::from_bool(true)}}),
                        ErrCode::Type),
               "paid arithmetic should report its type error");
}

bool test_traversal_semantic_boundaries() {
  for (TraversalDirection direction : {TraversalDirection::Forward,
                                       TraversalDirection::Reverse}) {
    const BytecodeProgram bytecode = compile(traversal(false, direction, 1, true));
    // Nine one-shot events, four cursor tests, six events for each of three
    // visits, and the enclosing RETURN: 9 + 4 + 18 + 1.
    constexpr int kMinimum = 32;
    if (!check(is_int(run(bytecode, kMinimum), 10),
               "forward/reverse traversal should meet its phase-derived minimum") ||
        !check(is_error(run(bytecode, kMinimum - 1), ErrCode::Timeout),
               "forward/reverse traversal should fail below that minimum")) {
      return false;
    }
  }

  const BytecodeProgram range =
      compile(traversal(true, TraversalDirection::Forward, 1, true));
  // Thirteen one-shot events, three cursor tests, six events for each of two
  // visits, and RETURN: 13 + 3 + 12 + 1.
  constexpr int kRangeMinimum = 29;
  return check(is_int(run(range, kRangeMinimum), 8),
               "range traversal should meet its phase-derived minimum") &&
         check(is_error(run(range, kRangeMinimum - 1), ErrCode::Timeout),
               "range traversal should fail below its phase-derived minimum");
}

bool test_nested_child_costs_do_not_inherit() {
  const BytecodeProgram bytecode =
      compile(traversal(false, TraversalDirection::Forward, 2, false));
  // The parent has 31 semantic events at cost two. Its three input leaves cost
  // one once, and the two body loads plus ADD cost one on each of three visits.
  // RETURN contributes the final unit: 62 + 3 + 9 + 1.
  constexpr int kMinimum = 75;
  return check(is_int(run(bytecode, kMinimum), 10),
               "child operations should retain their own default costs") &&
         check(is_error(run(bytecode, kMinimum - 1), ErrCode::Timeout),
               "nested child costs should be included independently");
}

bool test_zero_cycle_and_invalid_profiles() {
  AstProgram zero = traversal(false, TraversalDirection::Forward, 0, true);
  const auto verified = gagp::evo::verify_ast(zero, {});
  if (!check(verified.ok, "zero-cycle fixture should pass source verification")) {
    return false;
  }
  try {
    (void)gagp::evo::compile_for_eval(genome(zero), verified.verified);
    return check(false, "compiler should reject an all-zero traversal cycle");
  } catch (const std::invalid_argument&) {
  }

  AstProgram duplicate = return_program(leaf(NodeKind::CONST, 0),
                                        {Value::from_int(1)});
  duplicate.fuel_specs.push_back(
      {3, {{FuelEvent::Operation, 1}, {FuelEvent::Operation, 2}}});
  const auto bad = gagp::evo::verify_ast(duplicate, {});
  if (!check(!bad.ok && bad.diagnostic.code == VerifyCode::DuplicateMetadata &&
                 bad.diagnostic.path == "$.fuel_specs[0].charges[1].event",
             "duplicate source event diagnostic must retain its exact path")) return false;
  duplicate.fuel_specs[0].charges.resize(1);
  duplicate.fuel_specs.push_back(duplicate.fuel_specs[0]);
  const auto repeated = gagp::evo::verify_ast(duplicate, {});
  if (!check(!repeated.ok && repeated.diagnostic.code == VerifyCode::DuplicateMetadata &&
                 repeated.diagnostic.path == "$.fuel_specs[1].node_index",
             "duplicate source profile diagnostic must retain its exact path")) return false;
  duplicate.fuel_specs[1].node_index = duplicate.nodes.size();
  const auto outside = gagp::evo::verify_ast(duplicate, {});
  return check(!outside.ok && outside.diagnostic.code == VerifyCode::MetadataNodeMismatch &&
                   outside.diagnostic.path == "$.fuel_specs[1].node_index",
               "out-of-range fuel owner must be checked before indexing");
}

bool test_legacy_unprofiled_program() {
  const AstProgram ast = return_program(
      binary(NodeKind::ADD, leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1)),
      {Value::from_int(4), Value::from_int(6)});
  const BytecodeProgram bytecode = compile(ast);
  return check(bytecode.instruction_fuel.empty(),
               "ordinary unprofiled AST should retain an empty fuel schedule") &&
         check(is_error(run(bytecode, 3), ErrCode::Timeout),
               "legacy bytecode should still charge by instruction") &&
         check(is_int(run(bytecode, 4), 10),
               "legacy bytecode output and minimum should remain unchanged");
}

}  // namespace

int main() {
  gagp::payload::clear();
  if (!test_let_and_arithmetic_exact_boundary()) return 1;
  if (!test_lazy_branches_and_error_precedence()) return 1;
  if (!test_traversal_semantic_boundaries()) return 1;
  if (!test_nested_child_costs_do_not_inherit()) return 1;
  if (!test_zero_cycle_and_invalid_profiles()) return 1;
  if (!test_legacy_unprofiled_program()) return 1;
  std::cout << "gagp_test_source_fuel: OK\n";
  return 0;
}
