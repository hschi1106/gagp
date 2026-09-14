#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/bounded_region.hpp"
#include "typed_expr_analysis.hpp"

namespace {

using namespace gagp;
using namespace gagp::evo;
using gagp::evo::typed_expr::TypedExprRoot;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

RegionBound literal(std::int64_t value) {
  RegionBound bound;
  bound.literal = value;
  return bound;
}

RegionStateTransition offset(std::int64_t value) {
  RegionStateTransition transition;
  transition.kind = RegionTransitionKind::CoordinateOffset;
  transition.offset = value;
  return transition;
}

RegionPlan plan_with_requests(std::size_t count) {
  RegionPlan plan;
  plan.state_types = {ValueTag::Int};
  plan.result_type = ValueTag::Int;
  plan.coordinate_slots = {0};
  plan.coordinate_rank = {{0, 1}};
  plan.coordinate_domains = {{literal(0), literal(5)}};
  plan.requests = {{{offset(-1)}}};
  if (count == 2) plan.requests.push_back({{offset(-2)}});
  return plan;
}

AstProgram bounded_ast(std::size_t request_count = 2, int first_binder = 10) {
  AstProgram ast;
  ast.consts = {Value::from_int(2), Value::from_int(0)};
  ast.nodes = {
      {NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS}, {NodeKind::RETURN},
      {NodeKind::BOUNDED_REGION, 5, 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::EQ}, {NodeKind::REGION_VAR, first_binder + 0, 0},
      {NodeKind::CONST, 1, 0},
      {NodeKind::ADD}, {NodeKind::REGION_VAR, first_binder + 1, 0},
      {NodeKind::CONST, 1, 0},
      {NodeKind::ADD}, {NodeKind::REGION_VAR, first_binder + 2, 0},
      {NodeKind::CONST, 1, 0},
      {NodeKind::ADD}, {NodeKind::REGION_VAR, first_binder + 3, 0},
      {NodeKind::CONST, 1, 0},
      {NodeKind::BLOCK_NIL},
  };
  BoundedRegionSpec spec;
  spec.node_index = 3;
  spec.plan = plan_with_requests(request_count);
  spec.phases = {
      {1, {{{RegionSlotBank::State, 0}, first_binder + 0}}},
      {2, {{{RegionSlotBank::State, 0}, first_binder + 1}}},
      {3, {{{RegionSlotBank::Result, 0}, first_binder + 2}}},
      {4, {{{RegionSlotBank::State, 0}, first_binder + 3}}},
  };
  ast.bounded_region_specs = {spec};
  return ast;
}

AstProgram nested_initial_ast() {
  AstProgram inner = bounded_ast(2, 20);
  AstProgram ast;
  ast.consts = inner.consts;
  ast.consts.push_back(Value::from_bool(false));
  ast.nodes = {{NodeKind::PROGRAM}, {NodeKind::BLOCK_CONS},
               {NodeKind::RETURN}, {NodeKind::BOUNDED_REGION, 5, 0}};
  ast.nodes.insert(ast.nodes.end(), inner.nodes.begin() + 3,
                   inner.nodes.end() - 1);
  ast.nodes.push_back({NodeKind::CONST, 2, 0});
  ast.nodes.push_back({NodeKind::CONST, 0, 0});
  ast.nodes.push_back({NodeKind::CONST, 0, 0});
  ast.nodes.push_back({NodeKind::CONST, 0, 0});
  ast.nodes.push_back({NodeKind::BLOCK_NIL});

  BoundedRegionSpec outer;
  outer.node_index = 3;
  outer.plan = plan_with_requests(1);
  outer.phases = {{1, {}}, {2, {}}, {3, {}}, {4, {}}};
  BoundedRegionSpec nested = inner.bounded_region_specs[0];
  nested.node_index = 4;
  ast.bounded_region_specs = {outer, nested};
  return ast;
}

const TypedExprRoot* root_at(const std::vector<TypedExprRoot>& roots,
                             std::size_t start) {
  for (const TypedExprRoot& root : roots)
    if (root.start == start) return &root;
  return nullptr;
}

}  // namespace

int main() {
  using namespace gagp::evo::typed_expr;

  AstProgram ast = bounded_ast();
  VerifyOptions options;
  options.capture_exact_scopes = true;
  AstVerifyResult verified = verify_ast(ast, {}, options);
  if (!check(verified.ok, "bounded analysis fixture verifies: " +
                            verified.diagnostic.message)) return 1;

  const std::vector<TypedExprRoot> roots =
      collect_typed_expr_roots(ast, verified.verified);
  const TypedExprRoot* region = root_at(roots, 3);
  const TypedExprRoot* initial = root_at(roots, 4);
  const TypedExprRoot* predicate = root_at(roots, 5);
  const TypedExprRoot* predicate_constant = root_at(roots, 7);
  const TypedExprRoot* base = root_at(roots, 8);
  const TypedExprRoot* combine = root_at(roots, 11);
  const TypedExprRoot* boundary = root_at(roots, 14);
  if (!check(region && initial && predicate && predicate_constant && base &&
                 combine && boundary,
             "verified collection exposes region, operands, and phase roots")) {
    return 1;
  }
  if (!check(region->scheme_kind == static_cast<int>(NodeKind::BOUNDED_REGION) &&
                 region->phase_name == 0 && region->dp_dependency_arity == 2,
             "region root carries its scheme and request arity") ||
      !check(initial->scheme_kind == 0 &&
                 !is_asgp_phase_body_root(ast, verified.verified.subtree_end,
                                          *initial),
             "initial state stays outside phase protection") ||
      !check(predicate->phase_name ==
                     static_cast<int>(RegionPhaseKind::BasePredicate) + 1 &&
                 base->phase_name ==
                     static_cast<int>(RegionPhaseKind::BaseBody) + 1 &&
                 combine->phase_name ==
                     static_cast<int>(RegionPhaseKind::Combine) + 1 &&
                 boundary->phase_name ==
                     static_cast<int>(RegionPhaseKind::Boundary) + 1,
             "phase roots carry canonical bounded phase kinds") ||
      !check(predicate->dp_dependency_arity == 2 &&
                 base->dp_dependency_arity == 2 &&
                 is_asgp_phase_body_root(ast, verified.verified.subtree_end,
                                         *predicate),
             "bounded phase roots carry request arity and remain protected") ||
      !check(predicate->binder_signature ==
                     predicate_constant->binder_signature &&
                 predicate->binder_signature ==
                     verified.verified.expression_binder_signatures[5],
             "phase annotation preserves the exact verified bank scope")) {
    return 1;
  }

  if (!check(!typed_subtree_keys_compatible(*base, *combine),
             "compatibility rejects different bounded phase kinds")) return 1;
  TypedExprRoot changed = *base;
  changed.dp_dependency_arity += 1;
  if (!check(!typed_subtree_keys_compatible(*base, changed),
             "compatibility includes bounded request arity")) return 1;

  AstProgram rebound = bounded_ast(2, 30);
  AstVerifyResult rebound_verified = verify_ast(rebound, {}, options);
  if (!check(rebound_verified.ok, "rebound bounded fixture verifies")) return 1;
  const auto rebound_roots =
      collect_typed_expr_roots(rebound, rebound_verified.verified);
  const TypedExprRoot* rebound_base = root_at(rebound_roots, 8);
  if (!check(rebound_base != nullptr &&
                 rebound_base->phase_name == base->phase_name &&
                 rebound_base->dp_dependency_arity == base->dp_dependency_arity &&
                 !typed_subtree_keys_compatible(*base, *rebound_base),
             "compatibility includes the exact physical bank binder scope")) {
    return 1;
  }

  AstProgram one_request = bounded_ast(1);
  AstVerifyResult one_verified = verify_ast(one_request, {}, options);
  const auto one_roots = one_verified
      ? collect_typed_expr_roots(one_request, one_verified.verified)
      : std::vector<TypedExprRoot>{};
  const TypedExprRoot* one_base = root_at(one_roots, 8);
  if (!check(one_base && one_base->dp_dependency_arity == 1 &&
                 !typed_subtree_keys_compatible(*base, *one_base),
             "annotated request arity differs across valid plans")) return 1;

  const auto heuristic =
      collect_typed_expr_roots(ast, verified.verified.subtree_end);
  if (!check(heuristic.empty(),
             "legacy heuristic explicitly declines bounded lexical inference")) {
    return 1;
  }

  AstProgram nested = nested_initial_ast();
  AstVerifyResult nested_verified = verify_ast(nested, {}, options);
  if (!check(nested_verified.ok, "nested initial region verifies: " +
                                   nested_verified.diagnostic.message)) {
    return 1;
  }
  const auto nested_roots =
      collect_typed_expr_roots(nested, nested_verified.verified);
  const TypedExprRoot* nested_base = root_at(nested_roots, 9);
  if (!check(nested_base && nested_base->phase_name ==
                                  static_cast<int>(RegionPhaseKind::BaseBody) + 1 &&
                 nested_base->dp_dependency_arity == 2,
             "nested initial region phases use the nearest region owner")) {
    return 1;
  }

  return 0;
}
