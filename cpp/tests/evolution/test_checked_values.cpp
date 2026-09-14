#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/core/errors.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/grammar/catalog.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace {

using gagp::Value;
using namespace gagp::evo;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

AstProgram return_program(std::vector<AstNode> expression,
                          std::vector<Value> constants = {},
                          std::vector<std::string> names = {}) {
  AstProgram ast;
  ast.consts = std::move(constants);
  ast.names = std::move(names);
  ast.nodes = {
      {NodeKind::PROGRAM, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::RETURN, 0, 0},
  };
  ast.nodes.insert(ast.nodes.end(), expression.begin(), expression.end());
  ast.nodes.push_back({NodeKind::BLOCK_NIL, 0, 0});
  return ast;
}

ProgramGenome genome(const AstProgram& ast) {
  ProgramGenome out;
  out.ast = ast;
  out.meta = build_genome_meta(ast);
  return out;
}

gagp::ExecResult run(const AstProgram& ast,
                     const std::vector<InputSpec>& inputs = {},
                     const std::vector<Value>& values = {}) {
  const AstVerifyResult verified = verify_ast(ast, inputs);
  if (!verified.ok) {
    return {true, Value::invalid(),
            {gagp::ErrCode::Value,
             std::string("verification failed: ") +
                 verify_code_name(verified.diagnostic.code) + " " +
                 verified.diagnostic.message}};
  }
  std::vector<std::string> names;
  std::vector<std::pair<int, Value>> runtime_inputs;
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    names.push_back(inputs[i].name);
    runtime_inputs.push_back({static_cast<int>(i), values[i]});
  }
  return gagp::execute_bytecode_cpu(
      compile_for_eval(genome(ast), verified.verified, names), runtime_inputs,
      1000);
}

bool expect_type_mismatch(const AstProgram& ast, const std::string& label) {
  const AstVerifyResult result = verify_ast(ast, {});
  return check(!result.ok && result.diagnostic.code == VerifyCode::TypeMismatch,
               label);
}

bool test_native_typing_and_valid_execution() {
  AstProgram checked_int = return_program(
      {{NodeKind::CHECK_INT, 0, 0}, {NodeKind::CONST, 0, 0}},
      {Value::from_int(17)});
  const AstVerifyResult int_verified = verify_ast(checked_int, {});
  const gagp::ExecResult int_result = run(checked_int);
  if (!check(int_verified.ok && int_verified.verified.return_type == RType::Int,
             "CHECK_INT should retain exact Int type") ||
      !check(!int_result.is_error && int_result.value.tag == gagp::ValueTag::Int &&
                 int_result.value.i == 17,
             "CHECK_INT should return a valid Int unchanged")) {
    return false;
  }

  const std::vector<std::pair<RType, Value>> lists = {
      {RType::IntList,
       gagp::payload::make_int_list_value({Value::from_int(1)})},
      {RType::FloatList,
       gagp::payload::make_float_list_value({Value::from_float(1.5)})},
      {RType::StringList,
       gagp::payload::make_string_list_value(
           {gagp::payload::make_string_value("x")})},
  };
  for (const auto& item : lists) {
    AstProgram ast = return_program(
        {{NodeKind::CHECK_LIST, 0, 0}, {NodeKind::CONST, 0, 0}}, {item.second});
    const AstVerifyResult verified = verify_ast(ast, {});
    const gagp::ExecResult result = run(ast);
    if (!check(verified.ok && verified.verified.return_type == item.first,
               "CHECK_LIST should retain its exact typed-list type") ||
        !check(!result.is_error && result.value.tag == item.second.tag &&
                   result.value.i == item.second.i,
               "CHECK_LIST should return a valid typed list unchanged")) {
      return false;
    }
  }

  if (!expect_type_mismatch(
          return_program({{NodeKind::CHECK_INT, 0, 0},
                          {NodeKind::CONST, 0, 0}},
                         {Value::from_bool(true)}),
          "CHECK_INT should reject statically non-Int operands")) {
    return false;
  }
  return expect_type_mismatch(
      return_program({{NodeKind::CHECK_LIST, 0, 0},
                      {NodeKind::CONST, 0, 0}},
                     {gagp::payload::make_string_value("not a typed list")}),
      "CHECK_LIST should reject String operands");
}

bool test_dynamic_errors_laziness_and_ordering() {
  AstProgram checked_start = return_program(
      {{NodeKind::CHECK_INT, 0, 0}, {NodeKind::VAR, 0, 0}}, {}, {"start"});
  gagp::ExecResult result = run(
      checked_start, {{"start", RType::Int}}, {Value::from_bool(false)});
  if (!check(result.is_error && result.err.code == gagp::ErrCode::Type &&
                 result.err.message == "expected int",
             "CHECK_INT should preserve runtime CheckInt failure")) {
    return false;
  }

  AstProgram checked_list = return_program(
      {{NodeKind::CHECK_LIST, 0, 0}, {NodeKind::VAR, 0, 0}}, {}, {"xs"});
  result = run(checked_list, {{"xs", RType::IntList}},
               {gagp::payload::make_string_value("not a list")});
  if (!check(result.is_error && result.err.code == gagp::ErrCode::Type &&
                 result.err.message == "structured list source must be a typed list",
             "CHECK_LIST should preserve runtime CheckList failure")) {
    return false;
  }

  AstProgram lazy = return_program({
      {NodeKind::IF_EXPR, 0, 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::CONST, 1, 0},
      {NodeKind::CHECK_INT, 0, 0},
      {NodeKind::VAR, 0, 0},
  }, {Value::from_bool(true), Value::from_int(7)}, {"start"});
  result = run(lazy, {{"start", RType::Int}}, {Value::from_bool(false)});
  if (!check(!result.is_error && result.value.tag == gagp::ValueTag::Int &&
                 result.value.i == 7,
             "unselected checked branch should remain lazy")) {
    return false;
  }

  const std::vector<AstNode> checked_int_then_list = {
      {NodeKind::ADD, 0, 0},
      {NodeKind::CHECK_INT, 0, 0},
      {NodeKind::VAR, 0, 0},
      {NodeKind::CALL_LEN, 0, 0},
      {NodeKind::CHECK_LIST, 0, 0},
      {NodeKind::VAR, 1, 0},
  };
  AstProgram ordered = return_program(checked_int_then_list, {}, {"start", "xs"});
  result = run(ordered, {{"start", RType::Int}, {"xs", RType::IntList}},
               {Value::from_bool(false), gagp::payload::make_string_value("bad")});
  if (!check(result.is_error && result.err.message == "expected int",
             "left checked operand should fail before the right operand")) {
    return false;
  }

  AstProgram reversed = return_program({
      {NodeKind::ADD, 0, 0},
      {NodeKind::CALL_LEN, 0, 0},
      {NodeKind::CHECK_LIST, 0, 0},
      {NodeKind::VAR, 1, 0},
      {NodeKind::CHECK_INT, 0, 0},
      {NodeKind::VAR, 0, 0},
  }, {}, {"start", "xs"});
  result = run(reversed, {{"start", RType::Int}, {"xs", RType::IntList}},
               {Value::from_bool(false), gagp::payload::make_string_value("bad")});
  return check(result.is_error &&
                   result.err.message ==
                       "structured list source must be a typed list",
               "reversing checked operands should reverse error precedence");
}

bool test_catalog_signatures() {
  const auto& catalog = grammar::PrimitiveCatalog::standard();
  const auto& checked_int =
      catalog.resolve("check_int", {RType::Int}, RType::Int);
  if (!check(checked_int.lowering_node == NodeKind::CHECK_INT &&
                 checked_int.executable(),
             "check_int catalog lowering missing")) {
    return false;
  }
  for (RType list : {RType::IntList, RType::FloatList, RType::StringList}) {
    const auto& checked = catalog.resolve("check_list", {list}, list);
    if (!check(checked.lowering_node == NodeKind::CHECK_LIST &&
                   checked.executable(),
               "check_list catalog lowering missing")) {
      return false;
    }
  }
  try {
    (void)catalog.resolve("check_list", {RType::String}, RType::String);
  } catch (const std::invalid_argument&) {
    return true;
  }
  return check(false, "String check_list overload should not exist");
}

}  // namespace

int main() {
  if (!test_native_typing_and_valid_execution()) return 1;
  if (!test_dynamic_errors_laziness_and_ordering()) return 1;
  if (!test_catalog_signatures()) return 1;
  return 0;
}
