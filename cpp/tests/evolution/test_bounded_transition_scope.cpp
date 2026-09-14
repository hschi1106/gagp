#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/core/bytecode.hpp"
#include "gagp/core/errors.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/input_spec.hpp"
#include "gagp/evolution/transition/bounded_regions.hpp"
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
using gagp::evo::FuelEvent;
using gagp::evo::InputSpec;
using gagp::evo::NodeKind;
using gagp::evo::ProgramGenome;
using gagp::evo::RType;
using gagp::evo::TraversalDirection;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << '\n';
  return condition;
}

void append(Expr* out, const Expr& child) {
  out->insert(out->end(), child.begin(), child.end());
}

Expr leaf(NodeKind kind, int i0 = 0, int i1 = 0) {
  return {{kind, i0, i1}};
}

Expr expression(NodeKind kind, std::initializer_list<Expr> children,
                int i0 = 0, int i1 = 0) {
  Expr out{{kind, i0, i1}};
  for (const auto& child : children) append(&out, child);
  return out;
}

Expr binary(NodeKind kind, Expr left, Expr right) {
  return expression(kind, {std::move(left), std::move(right)});
}

AstProgram return_program(Expr root, std::vector<Value> constants,
                          std::vector<std::string> names) {
  AstProgram ast;
  ast.consts = std::move(constants);
  ast.names = std::move(names);
  ast.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN}};
  append(&ast.nodes, root);
  ast.nodes.push_back({NodeKind::BLOCK_NIL});
  return ast;
}

ProgramGenome genome(AstProgram ast) {
  ProgramGenome out;
  out.ast = std::move(ast);
  out.meta = gagp::evo::build_genome_meta(out.ast);
  return out;
}

std::size_t find_node(const AstProgram& ast, NodeKind kind,
                      std::size_t ordinal = 0) {
  for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
    if (ast.nodes[i].kind == kind && ordinal-- == 0) return i;
  }
  throw std::logic_error("fixture node was not found");
}

BytecodeProgram compile_checked(const ProgramGenome& source) {
  const auto verified = gagp::evo::verify_ast(source.ast, {});
  if (!verified) {
    throw std::runtime_error(std::string("fixture verification failed: ") +
                             gagp::evo::verify_code_name(verified.diagnostic.code) +
                             " " + verified.diagnostic.message);
  }
  return gagp::evo::compile_for_eval(source, verified.verified);
}

bool same_result(const ExecResult& left, const ExecResult& right) {
  if (left.is_error != right.is_error) return false;
  if (left.is_error) return left.err.code == right.err.code;
  if (left.value.tag != right.value.tag) return false;
  if (left.value.tag == ValueTag::Int || left.value.tag == ValueTag::Char)
    return left.value.i == right.value.i;
  if (left.value.tag == ValueTag::Bool) return left.value.b == right.value.b;
  return false;
}

int first_non_timeout(const BytecodeProgram& program) {
  for (int fuel = 0; fuel <= 10000; ++fuel) {
    const auto result = gagp::execute_bytecode_cpu(program, {}, fuel);
    if (!result.is_error || result.err.code != ErrCode::Timeout) return fuel;
  }
  return -1;
}

bool differential(const ProgramGenome& source, const std::string& label,
                  ProgramGenome* lowered_out = nullptr) {
  const auto before = compile_checked(source);
  ProgramGenome lowered =
      gagp::evo::transition::lower_bounded_regions(source);
  const auto after = compile_checked(lowered);
  const int boundary = first_non_timeout(before);
  if (!check(boundary >= 0, label + ": no finite fuel boundary")) return false;
  for (int fuel = 0; fuel <= boundary + 3; ++fuel) {
    const auto old_result = gagp::execute_bytecode_cpu(before, {}, fuel);
    const auto new_result = gagp::execute_bytecode_cpu(after, {}, fuel);
    if (!check(same_result(old_result, new_result),
               label + ": result differs at fuel " + std::to_string(fuel))) {
      return false;
    }
  }
  if (lowered_out != nullptr) *lowered_out = std::move(lowered);
  return true;
}

bool globally_unique_binders(const AstProgram& ast) {
  std::set<int> ids;
  for (const auto& region : ast.lexical_regions)
    for (const auto& binding : region.bindings)
      if (!ids.insert(binding.id).second) return false;
  for (const auto& region : ast.bounded_region_specs)
    for (const auto& phase : region.phases)
      for (const auto& binding : phase.bindings)
        if (!ids.insert(binding.binder_id).second) return false;
  return true;
}

ProgramGenome dc_scope_fixture() {
  // The Map binder deliberately shadows solve's n, and the Filter binder
  // shadows combine's left. Outer uses on both sides make a bad rewrite visible.
  const Expr mapped = expression(
      NodeKind::MAP_LIST,
      {leaf(NodeKind::BOUND_VAR, 0),
       binary(NodeKind::ADD, leaf(NodeKind::BOUND_VAR, 1),
              leaf(NodeKind::CONST, 1))},
      1, static_cast<int>(gagp::evo::ListTypeTag::Int));
  const Expr solve = binary(
      NodeKind::ADD,
      binary(NodeKind::ADD, leaf(NodeKind::BOUND_VAR, 1),
             leaf(NodeKind::BOUND_VAR, 2)),
      expression(NodeKind::CALL_LEN, {mapped}));
  const Expr divide = binary(NodeKind::SUB, leaf(NodeKind::BOUND_VAR, 3),
                             leaf(NodeKind::CONST, 1));
  const Expr filtered = expression(
      NodeKind::FILTER_LIST,
      {leaf(NodeKind::CONST, 3),
       binary(NodeKind::GT, leaf(NodeKind::BOUND_VAR, 4),
              leaf(NodeKind::CONST, 2))},
      4);
  const Expr combine = binary(
      NodeKind::SUB,
      binary(NodeKind::ADD, leaf(NodeKind::BOUND_VAR, 4),
             expression(NodeKind::CALL_LEN, {filtered})),
      leaf(NodeKind::BOUND_VAR, 5));
  Expr root = expression(NodeKind::ASGP_DC,
                         {leaf(NodeKind::CONST, 0), solve, divide, combine});
  AstProgram ast = return_program(
      std::move(root),
      {gagp::payload::make_int_list_value(
           {Value::from_int(2), Value::from_int(4), Value::from_int(6),
            Value::from_int(8)}),
       Value::from_int(1), Value::from_int(0),
       gagp::payload::make_int_list_value(
           {Value::from_int(3), Value::from_int(-1)})},
      {"xs", "n", "lo", "divide_n", "left", "right"});
  ast.asgp_dc_binders = {{3, 0, 1, 2, 3, 4, 5}};
  return genome(std::move(ast));
}

ProgramGenome dp1_scope_and_sidecar_fixture() {
  const Expr traversal = expression(
      NodeKind::TRAVERSE,
      {leaf(NodeKind::CONST, 1), leaf(NodeKind::BOUND_VAR, 0),
       leaf(NodeKind::CONST, 2),
       binary(NodeKind::SUB,
              binary(NodeKind::ADD, leaf(NodeKind::REGION_VAR, 101),
                     leaf(NodeKind::REGION_VAR, 100)),
              leaf(NodeKind::REGION_VAR, 102))});
  const Expr transition = binary(
      NodeKind::SUB,
      binary(NodeKind::ADD,
             binary(NodeKind::MUL, leaf(NodeKind::BOUND_VAR, 1),
                    leaf(NodeKind::CONST, 3)),
             leaf(NodeKind::BOUND_VAR, 2)),
      leaf(NodeKind::BOUND_VAR, 3));
  Expr root = expression(NodeKind::ASGP_DP1D,
                         {leaf(NodeKind::CONST, 0), traversal, transition});
  AstProgram ast = return_program(
      std::move(root),
      {Value::from_int(3),
       gagp::payload::make_int_list_value(
           {Value::from_int(4), Value::from_int(1)}),
       Value::from_int(0), Value::from_int(10), Value::from_int(7)},
      {"solve_state", "transition_state", "dep0", "dep1"});
  ast.asgp_dp1d_specs = {{3, 0, 3, 0, 4, NodeKind::DP1_BACKWARD2,
                          {1, 2}, 0, 1, {2, 3}}};
  const auto traversal_owner = find_node(ast, NodeKind::TRAVERSE);
  ast.lexical_regions = {{traversal_owner, 3,
                          {{100, RType::Int}, {102, RType::Int},
                           {101, RType::Int}}}};
  ast.traversal_specs = {{traversal_owner, TraversalDirection::Reverse}};
  ast.fuel_specs = {{traversal_owner, {{FuelEvent::Repeat, 2}}}};
  return genome(std::move(ast));
}

ProgramGenome dp2_binding_order_fixture() {
  const Expr solve = binary(NodeKind::SUB, leaf(NodeKind::BOUND_VAR, 0),
                            leaf(NodeKind::BOUND_VAR, 1));
  const Expr transition = binary(
      NodeKind::ADD,
      binary(NodeKind::ADD,
             binary(NodeKind::MUL, leaf(NodeKind::BOUND_VAR, 2),
                    leaf(NodeKind::CONST, 3)),
             binary(NodeKind::MUL, leaf(NodeKind::BOUND_VAR, 3),
                    leaf(NodeKind::CONST, 4))),
      binary(NodeKind::SUB,
             binary(NodeKind::MUL, leaf(NodeKind::BOUND_VAR, 4),
                    leaf(NodeKind::CONST, 5)),
             leaf(NodeKind::BOUND_VAR, 5)));
  Expr root = expression(NodeKind::ASGP_DP2D,
                         {leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
                          solve, transition});
  AstProgram ast = return_program(
      std::move(root),
      {Value::from_int(2), Value::from_int(2), Value::from_int(7),
       Value::from_int(1000), Value::from_int(100), Value::from_int(10)},
      {"solve_i", "solve_j", "transition_i", "transition_j", "dep0", "dep1"});
  ast.asgp_dp2d_specs = {{3, 0, 2, 0, 2, 0, 0, 2,
                          NodeKind::DP2_CROSS_BACKWARD,
                          0, 1, 2, 3, {4, 5}}};
  return genome(std::move(ast));
}

ProgramGenome nested_composition_fixture() {
  const Expr linear = expression(
      NodeKind::LINEAR_REC,
      {leaf(NodeKind::CONST, 0), leaf(NodeKind::CONST, 1),
       leaf(NodeKind::CONST, 2), leaf(NodeKind::CONST, 2),
       leaf(NodeKind::CONST, 2)});
  const Expr dc = expression(
      NodeKind::ASGP_DC,
      {linear,
       binary(NodeKind::ADD, leaf(NodeKind::BOUND_VAR, 4),
              leaf(NodeKind::BOUND_VAR, 5)),
       leaf(NodeKind::BOUND_VAR, 6),
       binary(NodeKind::ADD, leaf(NodeKind::BOUND_VAR, 7),
              leaf(NodeKind::BOUND_VAR, 8))});
  const Expr dp = expression(
      NodeKind::ASGP_DP1D,
      {dc, leaf(NodeKind::CONST, 3),
       binary(NodeKind::ADD, leaf(NodeKind::BOUND_VAR, 10),
              leaf(NodeKind::BOUND_VAR, 11))});
  AstProgram ast = return_program(
      dp,
      {gagp::payload::make_int_list_value({}), Value::from_int(0),
       gagp::payload::make_int_list_value(
           {Value::from_int(5), Value::from_int(6)}),
       Value::from_int(1), Value::from_int(8)},
      {"elem", "accum", "index", "xs", "n", "lo", "divide_n", "left",
       "right", "solve_state", "transition_state", "dep"});
  const auto linear_owner = find_node(ast, NodeKind::LINEAR_REC);
  const auto dc_owner = find_node(ast, NodeKind::ASGP_DC);
  const auto dp_owner = find_node(ast, NodeKind::ASGP_DP1D);
  ast.linear_rec_binders = {{linear_owner, 0, 1, 2}};
  ast.asgp_dc_binders = {{dc_owner, 3, 4, 5, 6, 7, 8}};
  ast.asgp_dp1d_specs = {{dp_owner, 0, 3, 0, 4,
                          NodeKind::DP1_BACKWARD1, {1}, 9, 10, {11}}};
  return genome(std::move(ast));
}

bool test_dc_scope() {
  ProgramGenome lowered;
  if (!differential(dc_scope_fixture(), "DC phase scope and shadowing", &lowered))
    return false;
  if (!check(lowered.ast.bounded_region_specs.size() == 1,
             "DC should produce one bounded region") ||
      !check(lowered.ast.bounded_region_specs[0].phases.size() == 5,
             "DC should expose predicate, solve, divide, request, combine phases") ||
      !check(globally_unique_binders(lowered.ast),
             "DC phase declarations are not globally fresh")) return false;
  const auto& phases = lowered.ast.bounded_region_specs[0].phases;
  return check(phases[1].bindings.size() == 3 &&
                   phases[2].bindings.size() == 1 &&
                   phases[4].bindings.size() == 2,
               "DC phase visibility does not match solve/divide/combine roles");
}

bool test_dp_scope_and_metadata() {
  ProgramGenome lowered;
  if (!differential(dp1_scope_and_sidecar_fixture(),
                    "DP1 binding order and retained sidecars", &lowered)) return false;
  if (!check(lowered.ast.lexical_regions.size() == 1 &&
                 lowered.ast.traversal_specs.size() == 1 &&
                 !lowered.ast.fuel_specs.empty(),
             "DP1 transition dropped retained region sidecars") ||
      !check(lowered.ast.nodes[lowered.ast.lexical_regions[0].node_index].kind ==
                 NodeKind::TRAVERSE &&
                 lowered.ast.traversal_specs[0].node_index ==
                 lowered.ast.lexical_regions[0].node_index &&
                 lowered.ast.traversal_specs[0].direction ==
                 TraversalDirection::Reverse,
             "DP1 transition did not shift traversal owners together") ||
      !check(globally_unique_binders(lowered.ast),
             "DP1 phase IDs collide with retained traversal IDs")) return false;
  bool retained_repeat = false;
  for (const auto& spec : lowered.ast.fuel_specs)
    if (spec.node_index == lowered.ast.lexical_regions[0].node_index)
      for (const auto& charge : spec.charges)
        retained_repeat |= charge.event == FuelEvent::Repeat && charge.cost == 2;
  return check(retained_repeat, "DP1 transition lost retained traversal fuel");
}

bool test_dp2_and_nested_composition() {
  if (!differential(dp2_binding_order_fixture(),
                    "DP2 noncommutative state/result order")) return false;
  ProgramGenome lowered;
  if (!differential(nested_composition_fixture(),
                    "nested LinearRec/DC/DP composition", &lowered)) return false;
  for (const auto& node : lowered.ast.nodes) {
    if (!check(node.kind != NodeKind::LINEAR_REC &&
                   node.kind != NodeKind::ASGP_DC &&
                   node.kind != NodeKind::ASGP_DP1D &&
                   node.kind != NodeKind::ASGP_DP2D,
               "nested transition left a legacy structured node")) return false;
  }
  return check(lowered.ast.linear_rec_binders.empty() &&
                   lowered.ast.asgp_dc_binders.empty() &&
                   lowered.ast.asgp_dp1d_specs.empty() &&
                   lowered.ast.asgp_dp2d_specs.empty() &&
                   lowered.ast.bounded_region_specs.size() == 2 &&
                   globally_unique_binders(lowered.ast),
               "nested transition metadata or binder identity is incomplete");
}

}  // namespace

int main() {
  try {
    gagp::payload::clear();
    if (!test_dc_scope() || !test_dp_scope_and_metadata() ||
        !test_dp2_and_nested_composition()) return 1;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
