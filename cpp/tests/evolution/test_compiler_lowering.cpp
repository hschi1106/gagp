#include <iostream>
#include <stdexcept>
#include <string>
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

using gagp::Value;
using gagp::evo::AstNode;
using gagp::evo::AstProgram;
using gagp::evo::NodeKind;
using gagp::evo::ProgramGenome;
using gagp::evo::RType;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

ProgramGenome genome(AstProgram ast) {
  ProgramGenome out;
  out.meta = gagp::evo::build_genome_meta(ast);
  out.ast = std::move(ast);
  return out;
}

bool test_for_range_bound_is_evaluated_once() {
  AstProgram ast;
  ast.names = {"n", "count", "i"};
  ast.consts = {Value::from_int(0), Value::from_int(1)};
  ast.nodes = {
      {NodeKind::PROGRAM, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::ASSIGN, 1, 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::FOR_RANGE, 2, 0},
      {NodeKind::VAR, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::ASSIGN, 0, 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::ASSIGN, 1, 0},
      {NodeKind::ADD, 0, 0},
      {NodeKind::VAR, 1, 0},
      {NodeKind::CONST, 1, 0},
      {NodeKind::BLOCK_NIL, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::RETURN, 0, 0},
      {NodeKind::VAR, 1, 0},
      {NodeKind::BLOCK_NIL, 0, 0},
  };

  const std::vector<gagp::evo::InputSpec> inputs{{"n", RType::Int}};
  const auto verified = gagp::evo::verify_ast(ast, inputs);
  if (!check(verified.ok, "evaluate-once loop AST should verify")) return false;

  const auto bytecode = gagp::evo::compile_for_eval(genome(ast), {"n"});
  const auto result = gagp::execute_bytecode_cpu(
      bytecode, {{0, Value::from_int(3)}}, 20000);
  return check(!result.is_error && result.value.tag == gagp::ValueTag::Int &&
                   result.value.i == 3,
               "loop bound must be cached before the loop body mutates n");
}

AstProgram short_circuit_program(NodeKind logical, bool lhs) {
  AstProgram ast;
  ast.consts = {Value::from_bool(lhs), Value::from_float(1.0), Value::from_float(0.0)};
  ast.nodes = {
      {NodeKind::PROGRAM, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::RETURN, 0, 0},
      {logical, 0, 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::EQ, 0, 0},
      {NodeKind::DIV, 0, 0},
      {NodeKind::CONST, 1, 0},
      {NodeKind::CONST, 2, 0},
      {NodeKind::CONST, 2, 0},
      {NodeKind::BLOCK_NIL, 0, 0},
  };
  return ast;
}

bool test_logical_operators_short_circuit() {
  for (const auto& item :
       std::vector<std::pair<NodeKind, bool>>{{NodeKind::AND, false},
                                              {NodeKind::OR, true}}) {
    AstProgram ast = short_circuit_program(item.first, item.second);
    const auto verified = gagp::evo::verify_ast(ast, {});
    if (!check(verified.ok,
               std::string("short-circuit AST should verify: ") +
                   gagp::evo::verify_code_name(verified.diagnostic.code) + " " +
                   verified.diagnostic.message)) return false;
    const auto result = gagp::execute_bytecode_cpu(
        gagp::evo::compile_for_eval(genome(std::move(ast))), {}, 20000);
    if (!check(!result.is_error && result.value.tag == gagp::ValueTag::Bool &&
                   result.value.b == item.second,
               "short-circuit lowering must skip division by zero")) return false;
  }
  return true;
}

bool test_verified_compile_reuses_and_validates_annotations() {
  AstProgram ast = short_circuit_program(NodeKind::AND, false);
  const auto verified = gagp::evo::verify_ast(ast, {});
  if (!check(verified.ok, "verified compile fixture should verify")) return false;

  const ProgramGenome program = genome(ast);
  const auto bytecode = gagp::evo::compile_for_eval(program, verified.verified);
  const auto result = gagp::execute_bytecode_cpu(bytecode, {}, 20000);
  if (!check(!result.is_error && result.value.tag == gagp::ValueTag::Bool &&
                 !result.value.b,
             "verified compile should preserve execution semantics")) return false;

  gagp::evo::VerifiedAst malformed = verified.verified;
  malformed.subtree_end.pop_back();
  try {
    (void)gagp::evo::compile_for_eval(program, malformed);
  } catch (const std::invalid_argument&) {
    return true;
  }
  return check(false, "verified compile should reject annotations for another AST shape");
}

bool test_sequential_loops_reuse_only_hidden_locals() {
  AstProgram ast;
  ast.names = {"count", "i"};
  ast.consts = {Value::from_int(0), Value::from_int(1), Value::from_int(2)};
  ast.nodes = {{NodeKind::PROGRAM, 0, 0}, {NodeKind::BLOCK_CONS, 0, 0},
               {NodeKind::ASSIGN, 0, 0}, {NodeKind::CONST, 0, 0},
               {NodeKind::BLOCK_CONS, 0, 0}, {NodeKind::ASSIGN, 1, 0},
               {NodeKind::CONST, 0, 0}};
  for (int i = 0; i < 40; ++i) {
    const std::vector<AstNode> loop{
        {NodeKind::BLOCK_CONS, 0, 0}, {NodeKind::FOR_RANGE, 1, 0},
        {NodeKind::CONST, 2, 0}, {NodeKind::BLOCK_CONS, 0, 0},
        {NodeKind::ASSIGN, 0, 0}, {NodeKind::ADD, 0, 0},
        {NodeKind::VAR, 0, 0}, {NodeKind::CONST, 1, 0},
        {NodeKind::BLOCK_NIL, 0, 0}};
    ast.nodes.insert(ast.nodes.end(), loop.begin(), loop.end());
  }
  const std::vector<AstNode> tail{{NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::RETURN, 0, 0}, {NodeKind::ADD, 0, 0},
      {NodeKind::VAR, 0, 0}, {NodeKind::VAR, 1, 0}, {NodeKind::BLOCK_NIL, 0, 0}};
  ast.nodes.insert(ast.nodes.end(), tail.begin(), tail.end());
  const auto verified = gagp::evo::verify_ast(ast, {});
  if (!check(verified.ok, "sequential loop fixture must verify")) return false;
  const auto program = gagp::evo::compile_for_eval(genome(ast), verified.verified);
  if (!check(program.n_locals <= 64, "sequential loop temporaries must fit GPU limits"))
    return false;
  const auto result = gagp::execute_bytecode_cpu(program, {}, 10000);
  return check(!result.is_error && result.value.tag == gagp::ValueTag::Int &&
                   result.value.i == 81,
               "named count and loop variable must survive temporary reuse");
}

// Every G3P builtin node: AST typing, lowering and execution agree, and a
// mistyped call is rejected by the verifier.
bool test_g3p_builtin_nodes() {
  const auto I = [](std::int64_t v) { return Value::from_int(v); };
  const auto S = [](const std::string& v) { return gagp::payload::make_string_value(v); };
  struct Item {
    std::string label;
    std::vector<AstNode> expression;
    std::vector<Value> consts;
    Value expected;
  };
  const auto c = [](int i) { return AstNode{NodeKind::CONST, i, 0}; };
  const auto n = [](NodeKind k) { return AstNode{k, 0, 0}; };
  const Value yes = Value::from_bool(true);
  const std::vector<Item> items{
      {"insert", {n(NodeKind::CALL_LEN), n(NodeKind::CALL_INSERT), n(NodeKind::CALL_RANGE), c(0), c(1), c(2), c(3)},
       {I(0), I(4), I(1), I(9)}, I(5)},
      {"remove_at", {n(NodeKind::CALL_LEN), n(NodeKind::CALL_REMOVE_AT), n(NodeKind::CALL_RANGE), c(0), c(1), c(2)},
       {I(0), I(4), I(5)}, I(3)},
      {"set_at", {n(NodeKind::CALL_INDEX_MOD), n(NodeKind::CALL_SET_AT), n(NodeKind::CALL_RANGE), c(0), c(1), c(2), c(3), c(4)},
       {I(0), I(4), I(-1), I(42), I(3)}, I(42)},
      {"index_mod string", {n(NodeKind::CALL_LEN), n(NodeKind::CALL_INDEX_MOD), c(0), c(1)}, {S("abc"), I(-8)}, I(1)},
      {"contains list", {n(NodeKind::CALL_CONTAINS), n(NodeKind::CALL_RANGE), c(0), c(1), c(2)}, {I(0), I(4), I(3)}, yes},
      {"strip", {n(NodeKind::CALL_LEN), n(NodeKind::CALL_STRIP), c(0)}, {S("  ab ")}, I(2)},
      {"lstrip", {n(NodeKind::CALL_LEN), n(NodeKind::CALL_LSTRIP), c(0)}, {S("  ab ")}, I(3)},
      {"rstrip", {n(NodeKind::CALL_LEN), n(NodeKind::CALL_RSTRIP), c(0)}, {S("  ab ")}, I(4)},
      {"strip chars", {n(NodeKind::CALL_LEN), n(NodeKind::CALL_STRIP_CHARS), c(0), c(1)}, {S("xxhixx"), S("x")}, I(2)},
      {"lstrip chars", {n(NodeKind::CALL_LEN), n(NodeKind::CALL_LSTRIP_CHARS), c(0), c(1)}, {S("xxhixx"), S("x")}, I(4)},
      {"rstrip chars", {n(NodeKind::CALL_LEN), n(NodeKind::CALL_RSTRIP_CHARS), c(0), c(1)}, {S("xxhixx"), S("x")}, I(4)},
      {"startswith", {n(NodeKind::CALL_STARTSWITH), c(0), c(1)}, {S("hello"), S("he")}, yes},
      {"endswith", {n(NodeKind::CALL_ENDSWITH), c(0), c(1)}, {S("hello"), S("he")}, Value::from_bool(false)},
      {"capitalize", {n(NodeKind::EQ), n(NodeKind::CALL_CAPITALIZE), c(0), c(1)}, {S("hELLO"), S("Hello")}, yes},
  };
  for (const auto& item : items) {
    AstProgram ast;
    ast.consts = item.consts;
    ast.nodes = {n(NodeKind::PROGRAM), n(NodeKind::BLOCK_CONS), n(NodeKind::RETURN)};
    ast.nodes.insert(ast.nodes.end(), item.expression.begin(), item.expression.end());
    ast.nodes.push_back(n(NodeKind::BLOCK_NIL));
    const auto verified = gagp::evo::verify_ast(ast, {});
    if (!check(verified.ok, item.label + ": AST should verify")) return false;
    const auto bytecode = gagp::evo::compile_for_eval(genome(ast), verified.verified);
    const auto result = gagp::execute_bytecode_cpu(bytecode, {}, 1000);
    if (!check(!result.is_error && result.value.tag == item.expected.tag && result.value.i == item.expected.i,
               item.label + ": lowered program result")) return false;
    // A Bool in place of the first constant mistypes every node.
    AstProgram bad = ast;
    bad.consts[0] = Value::from_bool(true);
    if (!check(!gagp::evo::verify_ast(bad, {}).ok, item.label + ": mistyped argument must be rejected")) return false;
  }
  // The element must match the list: insert(IntList, Int, String) is rejected.
  AstProgram mixed;
  mixed.consts = {I(0), I(2), I(0), S("x")};
  mixed.nodes = {n(NodeKind::PROGRAM), n(NodeKind::BLOCK_CONS), n(NodeKind::RETURN), n(NodeKind::CALL_LEN),
                 n(NodeKind::CALL_INSERT), n(NodeKind::CALL_RANGE), c(0), c(1), c(2), c(3), n(NodeKind::BLOCK_NIL)};
  return check(!gagp::evo::verify_ast(mixed, {}).ok, "insert of a String into an IntList must be rejected");
}

}  // namespace

int main() {
  if (!test_g3p_builtin_nodes()) return 1;
  if (!test_for_range_bound_is_evaluated_once()) return 1;
  if (!test_sequential_loops_reuse_only_hidden_locals()) return 1;
  if (!test_logical_operators_short_circuit()) return 1;
  if (!test_verified_compile_reuses_and_validates_annotations()) return 1;
  for(const auto kind:{NodeKind::CALL_TRUNC_DIV0,NodeKind::CALL_TRUNC_MOD0}) {
    AstProgram ast;ast.consts={Value::from_int(-7),Value::from_int(3)};
    ast.nodes={{NodeKind::PROGRAM,0,0},{NodeKind::BLOCK_CONS,0,0},{NodeKind::RETURN,0,0},
               {kind,0,0},{NodeKind::CONST,0,0},{NodeKind::CONST,1,0},{NodeKind::BLOCK_NIL,0,0}};
    const auto verified=gagp::evo::verify_ast(ast,{});
    if(!check(verified.ok,"truncating builtin AST type contract"))return 1;
    const auto bytecode=gagp::evo::compile_for_eval(genome(ast),verified.verified);
    const auto result=gagp::execute_bytecode_cpu(bytecode,{},100);
    if(!check(!result.is_error && result.value.tag==gagp::ValueTag::Int &&
        result.value.i==(kind==NodeKind::CALL_TRUNC_DIV0?-2:-1),"truncating builtin lowering"))return 1;
    ast.consts[0]=Value::from_bool(true);
    if(!check(!gagp::evo::verify_ast(ast,{}).ok,"truncating builtin rejects Bool"))return 1;
  }
  std::cout << "gagp_test_compiler_lowering: OK\n";
  return 0;
}
