#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "gagp/core/errors.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/repro/pack.hpp"
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
                          std::vector<Value> consts = {}) {
  AstProgram ast;
  ast.consts = std::move(consts);
  ast.nodes = {
      {NodeKind::PROGRAM, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::RETURN, 0, 0},
  };
  ast.nodes.insert(ast.nodes.end(), expression.begin(), expression.end());
  ast.nodes.push_back({NodeKind::BLOCK_NIL, 0, 0});
  return ast;
}

gagp::ExecResult execute(const AstProgram& ast) {
  const AstVerifyResult verified = verify_ast(ast, {});
  if (!verified.ok) {
    return {true, Value::invalid(),
            {gagp::ErrCode::Value,
             std::string("verification failed: ") +
                 verify_code_name(verified.diagnostic.code) + " " +
                 verified.diagnostic.message}};
  }
  ProgramGenome genome;
  genome.ast = ast;
  genome.meta = build_genome_meta(ast);
  return gagp::execute_bytecode_cpu(
      compile_for_eval(genome, verified.verified), {}, 20000);
}

// let x1 = 10 in let x7 = 20 in (let x2 = 2 in x2) + (let x0 = 3 in x0 + x1)
bool test_nested_lexical_regions_and_compaction() {
  AstProgram program = return_program({
      {NodeKind::LET_REGION, 0, 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::LET_REGION, 0, 0},
      {NodeKind::CONST, 1, 0},
      {NodeKind::ADD, 0, 0},
      {NodeKind::LET_REGION, 0, 0},
      {NodeKind::CONST, 3, 0},
      {NodeKind::REGION_VAR, 2, 0},
      {NodeKind::LET_REGION, 0, 0},
      {NodeKind::CONST, 4, 0},
      {NodeKind::ADD, 0, 0},
      {NodeKind::REGION_VAR, 0, 0},
      {NodeKind::REGION_VAR, 1, 0},
  }, {Value::from_int(10), Value::from_int(20), Value::from_int(99),
      Value::from_int(2), Value::from_int(3)});
  program.lexical_regions = {
      {3, 1, {{1, RType::Int}}},
      {5, 1, {{7, RType::Int}}},
      {8, 1, {{2, RType::Int}}},
      {11, 1, {{0, RType::Int}}},
  };
  const gagp::ExecResult result = execute(program);
  if (!check(!result.is_error && result.value.tag == gagp::ValueTag::Int &&
                 result.value.i == 15,
             "nested lexical regions should type-check and execute without capture")) {
    return false;
  }

  ProgramGenome genome;
  genome.ast = program;
  genome.meta = build_genome_meta(program);
  const ProgramGenome compacted = repro::compact_genome_tables(genome);
  if (!check(compacted.ast.consts.size() == 4,
             "table compaction should drop the unused constant") ||
      !check(compacted.ast.lexical_regions.size() == program.lexical_regions.size(),
             "table compaction should retain lexical metadata")) {
    return false;
  }
  for (std::size_t i = 0; i < program.lexical_regions.size(); ++i) {
    if (!check(compacted.ast.lexical_regions[i].bindings[0].id ==
                   program.lexical_regions[i].bindings[0].id,
               "table compaction should preserve immutable lexical ids")) {
      return false;
    }
  }
  for (std::size_t i = 0; i < program.nodes.size(); ++i) {
    if (program.nodes[i].kind == NodeKind::REGION_VAR &&
        !check(compacted.ast.nodes[i].i0 == program.nodes[i].i0,
               "table compaction should preserve region reference ids")) {
      return false;
    }
  }
  const gagp::ExecResult compacted_result = execute(compacted.ast);
  return check(!compacted_result.is_error && compacted_result.value.i == 15,
               "table compaction changed the result");
}

// (reverse traversal of [1,2,3] summing elements) + (let x20 = 4 in x20)
bool test_traversal_and_lexical_metadata() {
  AstProgram program = return_program({
      {NodeKind::ADD, 0, 0},
      {NodeKind::TRAVERSE, 0, 0},
      {NodeKind::CONST, 1, 0},
      {NodeKind::CONST, 2, 0},
      {NodeKind::CONST, 2, 0},
      {NodeKind::ADD, 0, 0},
      {NodeKind::REGION_VAR, 30, 0},
      {NodeKind::REGION_VAR, 32, 0},
      {NodeKind::LET_REGION, 0, 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::REGION_VAR, 20, 0},
  }, {Value::from_int(4),
      gagp::payload::make_int_list_value(
          {Value::from_int(1), Value::from_int(2), Value::from_int(3)}),
      Value::from_int(0)});
  program.lexical_regions = {
      {4, 3, {{30, RType::Int}, {31, RType::Int}, {32, RType::Int}}},
      {11, 1, {{20, RType::Int}}},
  };
  program.traversal_specs = {{4, TraversalDirection::Reverse}};
  const gagp::ExecResult result = execute(program);
  return check(!result.is_error && result.value.tag == gagp::ValueTag::Int &&
                   result.value.i == 10,
               "reverse traversal beside a lexical region should type-check and execute");
}

}  // namespace

int main() {
  if (!test_nested_lexical_regions_and_compaction()) return 1;
  if (!test_traversal_and_lexical_metadata()) return 1;
  return 0;
}
