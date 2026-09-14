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
#include "subtree_utils.hpp"

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

bool test_alpha_renames_collisions_and_preserves_free_references() {
  AstProgram base = return_program({
      {NodeKind::LET_REGION, 0, 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::LET_REGION, 0, 0},
      {NodeKind::CONST, 1, 0},
      {NodeKind::CONST, 2, 0},
  }, {Value::from_int(10), Value::from_int(20), Value::from_int(0)});
  base.lexical_regions = {
      {3, 1, {{1, RType::Int}}},
      {5, 1, {{7, RType::Int}}},
  };

  AstProgram donor;
  donor.consts = {Value::from_int(99), Value::from_int(2), Value::from_int(3)};
  donor.nodes = {
      {NodeKind::LET_REGION, 0, 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::ADD, 0, 0},
      {NodeKind::LET_REGION, 0, 0},
      {NodeKind::CONST, 1, 0},
      {NodeKind::REGION_VAR, 7, 0},
      {NodeKind::LET_REGION, 0, 0},
      {NodeKind::CONST, 2, 0},
      {NodeKind::ADD, 0, 0},
      {NodeKind::REGION_VAR, 0, 0},
      {NodeKind::REGION_VAR, 1, 0},
  };
  donor.lexical_regions = {
      {0, 1, {{1, RType::Int}}},
      {3, 1, {{7, RType::Int}}},
      {6, 1, {{0, RType::Int}}},
  };

  const AstProgram spliced = subtree::replace_subtree(base, 7, 8, donor, 2, 11);
  if (!check(spliced.lexical_regions.size() == 4,
             "splice should retain two base regions and copy two donor regions") ||
      !check(spliced.lexical_regions[0].node_index == 3 &&
                 spliced.lexical_regions[0].bindings[0].id == 1 &&
                 spliced.lexical_regions[1].node_index == 5 &&
                 spliced.lexical_regions[1].bindings[0].id == 7,
             "retained base lexical metadata should stay first and unchanged") ||
      !check(spliced.lexical_regions[2].node_index == 8 &&
                 spliced.lexical_regions[2].bindings[0].id == 2,
             "colliding donor id 7 should deterministically rename to 2") ||
      !check(spliced.lexical_regions[3].node_index == 11 &&
                 spliced.lexical_regions[3].bindings[0].id == 0,
             "non-colliding donor id 0 should be preserved") ||
      !check(spliced.nodes[10].kind == NodeKind::REGION_VAR &&
                 spliced.nodes[10].i0 == 2 &&
                 spliced.nodes[14].kind == NodeKind::REGION_VAR &&
                 spliced.nodes[14].i0 == 0,
             "introduced references should follow declaration alpha-renames") ||
      !check(spliced.nodes[15].kind == NodeKind::REGION_VAR &&
                 spliced.nodes[15].i0 == 1,
             "donor free reference should preserve its immutable id")) {
    return false;
  }

  const gagp::ExecResult result = execute(spliced);
  if (!check(!result.is_error && result.value.tag == gagp::ValueTag::Int &&
                 result.value.i == 15,
             "nested let splice should type-check and execute without capture")) {
    return false;
  }

  ProgramGenome genome;
  genome.ast = spliced;
  genome.meta = build_genome_meta(spliced);
  const ProgramGenome compacted = repro::compact_genome_tables(genome);
  if (!check(compacted.ast.lexical_regions.size() == spliced.lexical_regions.size(),
             "table compaction should retain lexical metadata")) {
    return false;
  }
  for (std::size_t i = 0; i < spliced.lexical_regions.size(); ++i) {
    if (!check(compacted.ast.lexical_regions[i].bindings[0].id ==
                   spliced.lexical_regions[i].bindings[0].id,
               "table compaction should preserve immutable lexical ids")) {
      return false;
    }
  }
  for (std::size_t i = 0; i < spliced.nodes.size(); ++i) {
    if (spliced.nodes[i].kind == NodeKind::REGION_VAR &&
        !check(compacted.ast.nodes[i].i0 == spliced.nodes[i].i0,
               "table compaction should preserve region reference ids")) {
      return false;
    }
  }
  return true;
}

bool test_shifts_base_metadata_and_copies_traversal_metadata() {
  AstProgram base = return_program({
      {NodeKind::ADD, 0, 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::LET_REGION, 0, 0},
      {NodeKind::CONST, 1, 0},
      {NodeKind::REGION_VAR, 20, 0},
  }, {Value::from_int(0), Value::from_int(4)});
  base.lexical_regions = {{5, 1, {{20, RType::Int}}}};

  AstProgram donor;
  donor.consts = {
      gagp::payload::make_int_list_value(
          {Value::from_int(1), Value::from_int(2), Value::from_int(3)}),
      Value::from_int(0),
      Value::from_int(0),
  };
  donor.nodes = {
      {NodeKind::TRAVERSE, 0, 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::CONST, 1, 0},
      {NodeKind::CONST, 2, 0},
      {NodeKind::ADD, 0, 0},
      {NodeKind::REGION_VAR, 30, 0},
      {NodeKind::REGION_VAR, 32, 0},
  };
  donor.lexical_regions = {{0, 3, {
      {30, RType::Int}, {31, RType::Int}, {32, RType::Int}}}};
  donor.traversal_specs = {{0, TraversalDirection::Reverse}};

  const AstProgram spliced = subtree::replace_subtree(base, 4, 5, donor, 0, 7);
  if (!check(spliced.lexical_regions.size() == 2 &&
                 spliced.lexical_regions[0].node_index == 11 &&
                 spliced.lexical_regions[0].bindings[0].id == 20,
             "retained base lexical metadata should shift by splice delta") ||
      !check(spliced.lexical_regions[1].node_index == 4 &&
                 spliced.lexical_regions[1].bindings[0].id == 30,
             "donor traversal lexical metadata should map into the splice") ||
      !check(spliced.traversal_specs.size() == 1 &&
                 spliced.traversal_specs[0].node_index == 4 &&
                 spliced.traversal_specs[0].direction == TraversalDirection::Reverse,
             "donor traversal direction metadata should be copied")) {
    return false;
  }
  const gagp::ExecResult result = execute(spliced);
  return check(!result.is_error && result.value.tag == gagp::ValueTag::Int &&
                   result.value.i == 10,
               "reverse traversal splice should type-check and execute");
}

}  // namespace

int main() {
  if (!test_alpha_renames_collisions_and_preserves_free_references()) return 1;
  if (!test_shifts_base_metadata_and_copies_traversal_metadata()) return 1;
  return 0;
}
