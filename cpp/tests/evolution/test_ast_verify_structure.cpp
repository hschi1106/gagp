#include <iostream>
#include <string>

#include "gagp/evolution/ast_verify.hpp"

namespace {

using namespace gagp::evo;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

AstProgram simple_program() {
  AstProgram ast;
  ast.consts = {gagp::Value::from_int(7)};
  ast.nodes = {
      AstNode{NodeKind::PROGRAM, 0, 0},
      AstNode{NodeKind::BLOCK_CONS, 0, 0},
      AstNode{NodeKind::RETURN, 0, 0},
      AstNode{NodeKind::CONST, 0, 0},
      AstNode{NodeKind::BLOCK_NIL, 0, 0},
  };
  return ast;
}

bool expect_code(const AstProgram& ast, VerifyCode expected, const std::string& label,
                 const VerifyOptions& options = VerifyOptions{},
                 const std::string& expected_path = {}) {
  const AstVerifyResult result = verify_ast_structure(ast, options);
  return check(!result, label + " should fail") &&
         check(result.diagnostic.code == expected,
               label + " expected " + verify_code_name(expected) + " but got " +
                   verify_code_name(result.diagnostic.code)) &&
         check(!result.diagnostic.path.empty(), label + " should report a path") &&
         check(expected_path.empty() || result.diagnostic.path == expected_path,
               label + " should retain the exact diagnostic path") &&
         check(!result.diagnostic.message.empty(), label + " should report a message");
}

}  // namespace

int main() {
  using namespace gagp::evo;

  const AstVerifyResult valid = verify_ast_structure(simple_program());
  if (!check(valid.ok, "simple program should verify") ||
      !check(valid.verified.subtree_end == std::vector<std::size_t>({5, 5, 4, 4, 5}),
             "simple subtree boundaries") ||
      !check(valid.verified.max_expression_depth == 1, "simple expression depth") ||
      !check(valid.verified.statement_count == 1, "simple statement count")) return 1;

  AstProgram ast = simple_program();
  ast.version = "ast-prefix-old";
  if (!expect_code(ast, VerifyCode::UnsupportedVersion, "old version")) return 1;

  ast = simple_program();
  ast.nodes.clear();
  if (!expect_code(ast, VerifyCode::EmptyProgram, "empty program")) return 1;

  ast = simple_program();
  ast.nodes[0].kind = static_cast<NodeKind>(999);
  if (!expect_code(ast, VerifyCode::UnknownNodeKind, "unknown kind")) return 1;

  ast = simple_program();
  ast.nodes[0].kind = NodeKind::BLOCK_NIL;
  if (!expect_code(ast, VerifyCode::InvalidRoot, "invalid root")) return 1;

  ast = simple_program();
  ast.nodes[2].kind = NodeKind::CONST;
  if (!expect_code(ast, VerifyCode::UnexpectedNodeCategory, "expression in statement slot")) return 1;

  ast = simple_program();
  ast.nodes.pop_back();
  if (!expect_code(ast, VerifyCode::TruncatedPrefix, "truncated block")) return 1;

  ast = simple_program();
  ast.nodes.push_back(AstNode{NodeKind::BLOCK_NIL, 0, 0});
  if (!expect_code(ast, VerifyCode::TrailingNodes, "trailing node")) return 1;

  ast = simple_program();
  ast.nodes[3].i0 = 1;
  if (!expect_code(ast, VerifyCode::ConstantIndexOutOfRange, "constant index", {}, "$.nodes[3].i0")) return 1;

  ast = simple_program();
  ast.nodes[3] = AstNode{NodeKind::VAR, 0, 0};
  if (!expect_code(ast, VerifyCode::NameIndexOutOfRange, "name index", {}, "$.nodes[3].i0")) return 1;

  ast = simple_program();
  ast.nodes[0].i1 = 1;
  if (!expect_code(ast, VerifyCode::InvalidIndexField, "unused index", {}, "$.nodes[0].i1")) return 1;

  ast = simple_program();
  ast.consts[0] = gagp::Value::invalid();
  if (!expect_code(ast, VerifyCode::InvalidConstantTag, "invalid constant tag")) return 1;

  VerifyOptions limits;
  limits.max_nodes = 4;
  if (!expect_code(simple_program(), VerifyCode::ResourceLimit, "node limit", limits)) return 1;

  limits = VerifyOptions{};
  limits.max_expression_depth = 1;
  ast = simple_program();
  ast.nodes[3] = AstNode{NodeKind::NEG, 0, 0};
  ast.nodes.insert(ast.nodes.begin() + 4, AstNode{NodeKind::CONST, 0, 0});
  if (!expect_code(ast, VerifyCode::ResourceLimit, "expression depth limit", limits)) return 1;

  return 0;
}
