#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "gagp/evolution/ast_verify.hpp"

namespace {

using namespace gagp::evo;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

const VerifiedScope& scope_at(const AstVerifyResult& result, std::size_t node_index) {
  return result.verified.scopes[result.verified.expression_scope_ids[node_index]];
}

bool check_scope(const VerifiedScope& scope,
                 const std::vector<std::pair<int, RType>>& locals,
                 const std::vector<std::pair<int, RType>>& binders,
                 const std::string& label) {
  return check(scope.locals == locals, label + " locals") &&
         check(scope.binders == binders, label + " binders");
}

VerifyOptions exact_scope_options() {
  VerifyOptions options;
  options.capture_exact_scopes = true;
  return options;
}

AstProgram assignment_program() {
  AstProgram ast;
  ast.names = {"input", "x", "y"};
  ast.consts = {gagp::Value::from_int(1)};
  ast.nodes = {
      {NodeKind::PROGRAM, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::ASSIGN, 1, 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::ASSIGN, 2, 0},
      {NodeKind::ADD, 0, 0},
      {NodeKind::VAR, 1, 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::RETURN, 0, 0},
      {NodeKind::ADD, 0, 0},
      {NodeKind::VAR, 1, 0},
      {NodeKind::VAR, 2, 0},
      {NodeKind::BLOCK_NIL, 0, 0},
  };
  return ast;
}

AstProgram branch_program() {
  AstProgram ast;
  ast.names = {"input", "then_only", "both"};
  ast.consts = {gagp::Value::from_bool(true), gagp::Value::from_int(1)};
  ast.nodes = {
      {NodeKind::PROGRAM, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::IF_STMT, 0, 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::ASSIGN, 2, 0},
      {NodeKind::CONST, 1, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::ASSIGN, 1, 0},
      {NodeKind::CONST, 1, 0},
      {NodeKind::BLOCK_NIL, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::ASSIGN, 2, 0},
      {NodeKind::CONST, 1, 0},
      {NodeKind::BLOCK_NIL, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::RETURN, 0, 0},
      {NodeKind::VAR, 2, 0},
      {NodeKind::BLOCK_NIL, 0, 0},
  };
  return ast;
}

AstProgram loop_program() {
  AstProgram ast;
  ast.names = {"i", "input", "loop_only"};
  ast.consts = {gagp::Value::from_int(3)};
  ast.nodes = {
      {NodeKind::PROGRAM, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::FOR_RANGE, 0, 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::ASSIGN, 2, 0},
      {NodeKind::VAR, 0, 0},
      {NodeKind::BLOCK_NIL, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::RETURN, 0, 0},
      {NodeKind::VAR, 1, 0},
      {NodeKind::BLOCK_NIL, 0, 0},
  };
  return ast;
}

}  // namespace

int main() {
  using namespace gagp::evo;

  AstProgram ast = assignment_program();
  AstVerifyResult result = verify_ast(ast, {InputSpec{"input", RType::Int}});
  if (!check(result.ok, "default verification succeeds") ||
      !check(result.verified.scopes.empty(), "default omits exact scope table") ||
      !check(result.verified.expression_scope_ids.empty(),
             "default omits exact expression scope IDs")) return 1;

  result = verify_ast(ast, {InputSpec{"input", RType::Int}}, exact_scope_options());
  if (!check(result.ok, "assignment exact-scope verification succeeds") ||
      !check(result.verified.expression_scope_ids.size() == ast.nodes.size(),
             "opt-in expression scope IDs match node count") ||
      !check(result.verified.expression_scope_ids[0] ==
                 std::numeric_limits<std::uint32_t>::max(),
             "non-expression retains sentinel") ||
      !check_scope(scope_at(result, 3), {{0, RType::Int}}, {},
                   "scope before first assignment") ||
      !check_scope(scope_at(result, 6), {{0, RType::Int}, {1, RType::Int}}, {},
                   "scope after first assignment") ||
      !check_scope(scope_at(result, 11),
                   {{0, RType::Int}, {1, RType::Int}, {2, RType::Int}}, {},
                   "scope after second assignment") ||
      !check(result.verified.expression_scope_ids[6] ==
                 result.verified.expression_scope_ids[7] &&
                 result.verified.expression_scope_ids[7] ==
                     result.verified.expression_scope_ids[8],
             "equal exact scopes share one ID")) return 1;

  ast = branch_program();
  result = verify_ast(ast, {InputSpec{"input", RType::Int}}, exact_scope_options());
  if (!check(result.ok, "branch exact-scope verification succeeds") ||
      !check_scope(scope_at(result, 17), {{0, RType::Int}, {2, RType::Int}}, {},
                   "post-branch scope is the exact branch intersection")) return 1;

  ast = loop_program();
  result = verify_ast(ast, {InputSpec{"input", RType::Int}}, exact_scope_options());
  if (!check(result.ok, "loop exact-scope verification succeeds") ||
      !check_scope(scope_at(result, 6), {{0, RType::Int}, {1, RType::Int}}, {},
                   "loop body includes the loop local") ||
      !check_scope(scope_at(result, 10), {{1, RType::Int}}, {},
                   "loop locals do not escape the body") ||
      !check(result.verified.expression_scope_ids[3] == result.verified.expression_scope_ids[10],
             "leaving the loop restores the original exact scope ID")) return 1;

  return 0;
}
