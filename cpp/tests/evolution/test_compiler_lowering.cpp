#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/core/bytecode_verify.hpp"
#include "gagp/core/errors.hpp"
#include "gagp/core/value.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/input_spec.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"

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


// while_stmt programs: count = 0; <prefix>; while_stmt(bound, cond, body);
// return count. Names: count, k, i, m. Constants 0..3 are 0, 1, true, false.
struct WhileProgram {
  AstProgram ast;
  WhileProgram() {
    ast.names = {"count", "k", "i", "m"};
    ast.consts = {Value::from_int(0), Value::from_int(1), Value::from_bool(true),
                  Value::from_bool(false)};
  }
  AstNode constant(const Value& value) {
    ast.consts.push_back(value);
    return {NodeKind::CONST, static_cast<int>(ast.consts.size()) - 1, 0};
  }
};
constexpr int k_count = 0, k_k = 1, k_i = 2, k_m = 3;
const AstNode k_zero{NodeKind::CONST, 0, 0}, k_one{NodeKind::CONST, 1, 0},
    k_true{NodeKind::CONST, 2, 0}, k_false{NodeKind::CONST, 3, 0};
std::vector<AstNode> increment(int name) {
  return {{NodeKind::BLOCK_CONS, 0, 0}, {NodeKind::ASSIGN, name, 0}, {NodeKind::ADD, 0, 0},
          {NodeKind::VAR, name, 0}, k_one};
}
std::vector<AstNode> cat(std::initializer_list<std::vector<AstNode>> parts) {
  std::vector<AstNode> out;
  for (const auto& part : parts) out.insert(out.end(), part.begin(), part.end());
  return out;
}
// The statements of a block, each preceded by BLOCK_CONS, then BLOCK_NIL.
std::vector<AstNode> block(std::initializer_list<std::vector<AstNode>> statements) {
  std::vector<AstNode> out;
  for (const auto& statement : statements) {
    out.push_back({NodeKind::BLOCK_CONS, 0, 0});
    out.insert(out.end(), statement.begin(), statement.end());
  }
  out.push_back({NodeKind::BLOCK_NIL, 0, 0});
  return out;
}
std::vector<AstNode> assign(int name, std::vector<AstNode> value) {
  value.insert(value.begin(), {NodeKind::ASSIGN, name, 0});
  return value;
}
std::vector<AstNode> loop(const std::vector<AstNode>& bound, const std::vector<AstNode>& cond,
                          const std::vector<AstNode>& body) {
  return cat({{{NodeKind::WHILE_STMT, 0, 0}}, bound, cond, body});
}
// count += 1, as a one-statement block body.
std::vector<AstNode> count_body() { return cat({increment(k_count), {{NodeKind::BLOCK_NIL, 0, 0}}}); }
AstProgram finish(WhileProgram& program, std::initializer_list<std::vector<AstNode>> statements) {
  std::vector<std::vector<AstNode>> all{assign(k_count, {k_zero})};
  all.insert(all.end(), statements.begin(), statements.end());
  all.push_back({{NodeKind::RETURN, 0, 0}, {NodeKind::VAR, k_count, 0}});
  program.ast.nodes = {{NodeKind::PROGRAM, 0, 0}};
  for (const auto& statement : all) {
    program.ast.nodes.push_back({NodeKind::BLOCK_CONS, 0, 0});
    program.ast.nodes.insert(program.ast.nodes.end(), statement.begin(), statement.end());
  }
  program.ast.nodes.push_back({NodeKind::BLOCK_NIL, 0, 0});
  return program.ast;
}
gagp::ExecResult run(const AstProgram& ast, int fuel = 20000) {
  const auto verified = gagp::evo::verify_ast(ast, {});
  if (!verified.ok)
    throw std::runtime_error(std::string("while fixture must verify: ") +
                             gagp::evo::verify_code_name(verified.diagnostic.code) + " " +
                             verified.diagnostic.message);
  const auto bytecode = gagp::evo::compile_for_eval(genome(ast), verified.verified);
  const auto checked = gagp::verify_bytecode(bytecode);
  if (!checked) throw std::runtime_error("while bytecode must verify");
  return gagp::execute_bytecode_cpu(bytecode, {}, fuel);
}
bool returns(const gagp::ExecResult& result, long long value, const std::string& message) {
  return check(!result.is_error && result.value.tag == gagp::ValueTag::Int && result.value.i == value,
               message + " (got " + (result.is_error ? std::string(gagp::err_code_name(result.err.code))
                                                    : std::to_string(result.value.i)) + ")");
}
bool fails(const gagp::ExecResult& result, gagp::ErrCode code, const std::string& message) {
  return check(result.is_error && result.err.code == code, message);
}
// while_stmt(bound, cond, count += 1), with an Int bound constant.
AstProgram counted(long long bound, const std::vector<AstNode>& cond, WhileProgram program = {}) {
  const auto b = program.constant(Value::from_int(bound));
  return finish(program, {loop({b}, cond, count_body())});
}

bool test_while_semantics() {
  const std::vector<AstNode> count_lt_5{{NodeKind::LT, 0, 0}, {NodeKind::VAR, k_count, 0}};
  WhileProgram five;
  auto lt5 = count_lt_5; lt5.push_back(five.constant(Value::from_int(5)));
  WhileProgram hundred;
  auto lt100 = count_lt_5; lt100.push_back(hundred.constant(Value::from_int(100)));
  const std::vector<AstNode> never_true{{NodeKind::LT, 0, 0}, {NodeKind::VAR, k_count, 0}, k_zero};
  if (!returns(run(counted(0, {k_true})), 0, "zero bound never runs the body")) return false;
  if (!fails(run(counted(-1, {k_true})), gagp::ErrCode::Type,
             "negative bound is a TypeError, as for for_range")) return false;
  if (!returns(run(counted(10, {k_false})), 0, "false condition at entry")) return false;
  if (!returns(run(counted(10, never_true)), 0, "condition false at entry from a local")) return false;
  if (!returns(run(counted(100, lt5, five)), 5, "condition turning false leaves the loop")) return false;
  if (!returns(run(counted(7, lt100, hundred)), 7, "bound reached leaves the loop quietly")) return false;
  if (!returns(run(counted(9, {k_true})), 9, "constant condition runs to the bound")) return false;
  // The condition reads a local the body never changes.
  {
    WhileProgram program;
    const auto bound = program.constant(Value::from_int(4));
    if (!returns(run(finish(program, {assign(k_k, {k_one}),
                                      loop({bound}, {{NodeKind::EQ, 0, 0}, {NodeKind::VAR, k_k, 0}, k_one},
                                           count_body())})),
                 4, "unchanging local condition runs to the bound")) return false;
  }
  // The bound is evaluated once: the body resets m, the loop still runs 3 times.
  {
    WhileProgram program;
    const auto three = program.constant(Value::from_int(3));
    if (!returns(run(finish(program, {assign(k_m, {three}),
                                      loop({{NodeKind::VAR, k_m, 0}}, {k_true},
                                           cat({{{NodeKind::BLOCK_CONS, 0, 0}}, assign(k_m, {k_zero}),
                                                count_body()}))})),
                 3, "bound is evaluated once")) return false;
  }
  // for_range(i, 3) { k = 0; while_stmt(10, k < i + 2, { k += 1; count += 1 }) }
  {
    WhileProgram program;
    const auto three = program.constant(Value::from_int(3));
    const auto ten = program.constant(Value::from_int(10));
    const auto two = program.constant(Value::from_int(2));
    const auto inner = loop({ten},
        {{NodeKind::LT, 0, 0}, {NodeKind::VAR, k_k, 0}, {NodeKind::ADD, 0, 0}, {NodeKind::VAR, k_i, 0}, two},
        cat({increment(k_k), count_body()}));
    const auto outer = cat({{{NodeKind::FOR_RANGE, k_i, 0}, three},
                            block({assign(k_k, {k_zero}), inner})});
    if (!returns(run(finish(program, {outer})), 9, "while_stmt nested in for_range")) return false;
  }
  // while_stmt(3, true, { for_range(i, 2) { count += 1 } })
  {
    WhileProgram program;
    const auto three = program.constant(Value::from_int(3));
    const auto two = program.constant(Value::from_int(2));
    const auto inner = cat({{{NodeKind::FOR_RANGE, k_i, 0}, two}, count_body()});
    if (!returns(run(finish(program, {loop({three}, {k_true}, block({inner}))})), 6,
                 "for_range nested in while_stmt")) return false;
  }
  // while_stmt(2, true, { while_stmt(3, true, { count += 1 }) })
  {
    WhileProgram program;
    const auto two = program.constant(Value::from_int(2));
    const auto three = program.constant(Value::from_int(3));
    const auto inner = loop({three}, {k_true}, count_body());
    if (!returns(run(finish(program, {loop({two}, {k_true}, block({inner}))})), 6,
                 "nested while_stmt")) return false;
  }
  // Fuel: a huge bound runs out of fuel, exactly like for_range.
  {
    const auto huge = counted(1000000000, {k_true});
    if (!fails(run(huge, 1000), gagp::ErrCode::Timeout, "fuel exhaustion inside while_stmt")) return false;
    WhileProgram program;
    const auto bound = program.constant(Value::from_int(1000000000));
    const auto ranged = finish(program, {cat({{{NodeKind::FOR_RANGE, k_i, 0}, bound}, count_body()})});
    const auto expected = run(ranged, 1000);
    if (!fails(expected, gagp::ErrCode::Timeout, "for_range reference fuel exhaustion")) return false;
  }
  // Every instruction costs one fuel. count = 0 (2), the bound and its check
  // (9), the counter (2), three iterations of 15 (bound test 4, condition 2,
  // body 4, increment 5), the final bound test (4) and the return (2): 64.
  {
    const auto three = counted(3, {k_true});
    if (!returns(run(three, 64), 3, "while_stmt fits its exact fuel")) return false;
    if (!fails(run(three, 63), gagp::ErrCode::Timeout, "while_stmt one fuel short")) return false;
  }
  return true;
}

bool test_while_lowering() {
  WhileProgram program;
  const auto three = program.constant(Value::from_int(3));
  const auto ast = finish(program, {loop({three}, {k_true}, block({}))});
  const auto verified = gagp::evo::verify_ast(ast, {});
  if (!check(verified.ok, "empty-body while_stmt must verify")) return false;
  const auto bytecode = gagp::evo::compile_for_eval(genome(ast), verified.verified);
  if (!check(bool(gagp::verify_bytecode(bytecode)), "while_stmt bytecode must verify")) return false;
  using gagp::Opcode;
  const std::vector<Opcode> expected{
      Opcode::PushConst, Opcode::Store,                                  // count = 0
      Opcode::PushConst, Opcode::Store, Opcode::Load, Opcode::CallBuiltin,
      Opcode::JmpIfFalse, Opcode::Load, Opcode::PushConst, Opcode::Lt,
      Opcode::JmpIfFalse, Opcode::PushConst, Opcode::Neg,                // bound check
      Opcode::PushConst, Opcode::Store,                                  // counter = 0
      Opcode::Load, Opcode::Load, Opcode::Lt, Opcode::JmpIfFalse,        // 15: counter < bound
      Opcode::PushConst, Opcode::JmpIfFalse,                             // 19: condition
      Opcode::Load, Opcode::PushConst, Opcode::Add, Opcode::Store, Opcode::Jmp,  // 21: next
      Opcode::Load, Opcode::Return};                                     // 26: return count
  if (!check(bytecode.code.size() == expected.size(), "while_stmt instruction count")) return false;
  for (std::size_t i = 0; i < expected.size(); ++i)
    if (!check(bytecode.code[i].op == expected[i], "while_stmt opcode " + std::to_string(i))) return false;
  return check(bytecode.code[18].a == 26 && bytecode.code[20].a == 26 && bytecode.code[25].a == 15,
               "both loop exits leave after the back edge, which returns to the bound test");
}

bool test_while_verifier_errors() {
  using gagp::evo::VerifyCode;
  const auto rejects = [](const AstProgram& ast, VerifyCode code, const std::string& message) {
    const auto verified = gagp::evo::verify_ast(ast, {});
    return check(!verified.ok && verified.diagnostic.code == code,
                 message + " (got " + gagp::evo::verify_code_name(verified.diagnostic.code) + ")");
  };
  {
    WhileProgram program;
    if (!rejects(finish(program, {loop({k_true}, {k_true}, count_body())}), VerifyCode::TypeMismatch,
                 "Bool bound")) return false;
  }
  {
    WhileProgram program;
    if (!rejects(finish(program, {loop({k_one}, {k_one}, count_body())}), VerifyCode::TypeMismatch,
                 "Int condition")) return false;
  }
  {
    // The condition cannot read a local that only the body assigns.
    WhileProgram program;
    if (!rejects(finish(program, {loop({k_one}, {{NodeKind::EQ, 0, 0}, {NodeKind::VAR, k_k, 0}, k_one},
                                       block({assign(k_k, {k_one})}))}),
                 VerifyCode::UndefinedLocal, "condition reads a body-only local")) return false;
  }
  {
    // A body assignment never defines a local after the loop.
    WhileProgram program;
    if (!rejects(finish(program, {loop({k_one}, {k_true}, block({assign(k_k, {k_one})})),
                                  assign(k_count, {{NodeKind::VAR, k_k, 0}})}),
                 VerifyCode::UndefinedLocal, "body-only local read after the loop")) return false;
  }
  {
    // A block in the condition position is a category error.
    WhileProgram program;
    if (!rejects(finish(program, {loop({k_one}, {{NodeKind::BLOCK_NIL, 0, 0}}, count_body())}),
                 VerifyCode::UnexpectedNodeCategory, "block as condition")) return false;
  }
  return true;
}
}  // namespace

int main() {
  if (!test_for_range_bound_is_evaluated_once()) return 1;
  if (!test_sequential_loops_reuse_only_hidden_locals()) return 1;
  if (!test_logical_operators_short_circuit()) return 1;
  if (!test_verified_compile_reuses_and_validates_annotations()) return 1;
  try {
    if (!test_while_semantics()) return 1;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
  if (!test_while_lowering()) return 1;
  if (!test_while_verifier_errors()) return 1;
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
