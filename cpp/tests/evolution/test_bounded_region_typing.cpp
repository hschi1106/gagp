#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/bounded_region.hpp"

namespace {

using namespace gagp;
using namespace gagp::evo;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << '\n';
  return condition;
}

bool expect(const AstProgram& ast, const std::vector<InputSpec>& inputs,
            VerifyCode code, const std::string& label) {
  const AstVerifyResult result = verify_ast(ast, inputs);
  return check(!result, label + " should fail") &&
         check(result.diagnostic.code == code,
               label + " expected " + verify_code_name(code) + ", got " +
                   verify_code_name(result.diagnostic.code));
}

RegionStateTransition offset(std::int64_t amount) {
  RegionStateTransition result;
  result.kind = RegionTransitionKind::CoordinateOffset;
  result.source_state = 0;
  result.offset = amount;
  return result;
}

RegionBound literal(std::int64_t value) {
  RegionBound result;
  result.kind = RegionBoundKind::Literal;
  result.literal = value;
  return result;
}

RegionBound operand(std::uint32_t index) {
  RegionBound result;
  result.kind = RegionBoundKind::Operand;
  result.operand = index;
  return result;
}

RegionPlan coordinate_plan(bool parameter = false, bool dynamic_bound = false) {
  RegionPlan plan;
  plan.state_types = {ValueTag::Int};
  plan.result_type = ValueTag::Int;
  if (parameter) plan.parameter_types = {ValueTag::Int};
  plan.bound_operand_count = dynamic_bound ? 1 : 0;
  plan.requests = {{{offset(-1)}}};
  plan.limits = {16, 0, 1};
  plan.progress = RegionProgressKind::Coordinates;
  plan.coordinate_slots = {0};
  plan.coordinate_rank = {{0, 1}};
  plan.coordinate_domains = {{literal(0), dynamic_bound ? operand(1) : literal(3)}};
  plan.coordinate_endpoint = DomainEndpoint::Inclusive;
  return plan;
}

std::vector<RegionAstPhase> phases(std::size_t first_argument,
                                   bool parameter_body = false,
                                   int parameter_binder = 10) {
  std::vector<RegionAstPhase> result{
      {static_cast<std::uint32_t>(first_argument), {}},
      {static_cast<std::uint32_t>(first_argument + 1), {}},
      {static_cast<std::uint32_t>(first_argument + 2), {}},
      {static_cast<std::uint32_t>(first_argument + 3), {}}};
  if (parameter_body) {
    result[1].bindings.push_back(
        {RegionValueSlot{RegionSlotBank::Parameter, 0}, parameter_binder});
  }
  return result;
}

AstProgram name_capture_program() {
  AstProgram ast;
  ast.names = {"x"};
  ast.consts = {Value::from_int(2), Value::from_bool(true), Value::from_int(8)};
  const RegionPlan plan = coordinate_plan(true);
  ast.nodes = {
      {NodeKind::PROGRAM, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::RETURN, 0, 0},
      {NodeKind::BOUNDED_REGION, static_cast<int>(bounded_region_arity(plan)), 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::CONST, 1, 0},
      {NodeKind::REGION_VAR, 10, 0},
      {NodeKind::CONST, 2, 0},
      {NodeKind::CONST, 2, 0},
      {NodeKind::BLOCK_NIL, 0, 0},
  };
  ast.bounded_region_specs = {
      {3, plan, {{RegionCaptureKind::Name, 0}}, phases(1, true)}};
  return ast;
}

AstProgram assigned_name_capture_program() {
  AstProgram ast = name_capture_program();
  ast.nodes = {
      {NodeKind::PROGRAM, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::ASSIGN, 0, 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::RETURN, 0, 0},
      {NodeKind::BOUNDED_REGION, 5, 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::CONST, 1, 0},
      {NodeKind::REGION_VAR, 10, 0},
      {NodeKind::CONST, 2, 0},
      {NodeKind::CONST, 2, 0},
      {NodeKind::BLOCK_NIL, 0, 0},
  };
  ast.bounded_region_specs[0].node_index = 6;
  return ast;
}

AstProgram branch_only_name_capture_program() {
  AstProgram ast = name_capture_program();
  ast.nodes = {
      {NodeKind::PROGRAM, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::IF_STMT, 0, 0},
      {NodeKind::CONST, 1, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::ASSIGN, 0, 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::BLOCK_NIL, 0, 0},
      {NodeKind::BLOCK_NIL, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::RETURN, 0, 0},
      {NodeKind::BOUNDED_REGION, 5, 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::CONST, 1, 0},
      {NodeKind::REGION_VAR, 10, 0},
      {NodeKind::CONST, 2, 0},
      {NodeKind::CONST, 2, 0},
      {NodeKind::BLOCK_NIL, 0, 0},
  };
  ast.bounded_region_specs[0].node_index = 11;
  return ast;
}

AstProgram lexical_capture_program() {
  AstProgram ast;
  ast.consts = {Value::from_int(2), Value::from_bool(true), Value::from_int(8)};
  const RegionPlan plan = coordinate_plan(true);
  ast.nodes = {
      {NodeKind::PROGRAM, 0, 0},
      {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::RETURN, 0, 0},
      {NodeKind::LET_REGION, 0, 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::BOUNDED_REGION, static_cast<int>(bounded_region_arity(plan)), 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::CONST, 1, 0},
      {NodeKind::REGION_VAR, 10, 0},
      {NodeKind::CONST, 2, 0},
      {NodeKind::CONST, 2, 0},
      {NodeKind::BLOCK_NIL, 0, 0},
  };
  ast.lexical_regions = {{3, 1, {{7, RType::Int}}}};
  ast.bounded_region_specs = {
      {5, plan, {{RegionCaptureKind::Lexical, 7}}, phases(1, true)}};
  return ast;
}

AstProgram dynamic_bound_program() {
  AstProgram ast;
  ast.consts = {Value::from_int(1), Value::from_float(3.0),
                Value::from_bool(true), Value::from_int(0)};
  const RegionPlan plan = coordinate_plan(false, true);
  ast.nodes = {
      {NodeKind::PROGRAM, 0, 0}, {NodeKind::BLOCK_CONS, 0, 0},
      {NodeKind::RETURN, 0, 0},
      {NodeKind::BOUNDED_REGION, static_cast<int>(bounded_region_arity(plan)), 0},
      {NodeKind::CONST, 0, 0}, {NodeKind::CONST, 1, 0},
      {NodeKind::CONST, 2, 0}, {NodeKind::CONST, 3, 0},
      {NodeKind::CONST, 3, 0}, {NodeKind::CONST, 3, 0},
      {NodeKind::BLOCK_NIL, 0, 0},
  };
  ast.bounded_region_specs = {{3, plan, {}, phases(2)}};
  return ast;
}

AstProgram nested_region(bool in_phase) {
  AstProgram ast;
  ast.consts = {Value::from_int(1), Value::from_bool(true), Value::from_int(0)};
  const RegionPlan plan = coordinate_plan();
  if (in_phase) {
    ast.nodes = {
        {NodeKind::PROGRAM, 0, 0}, {NodeKind::BLOCK_CONS, 0, 0},
        {NodeKind::RETURN, 0, 0}, {NodeKind::BOUNDED_REGION, 5, 0},
        {NodeKind::CONST, 0, 0}, {NodeKind::CONST, 1, 0},
        {NodeKind::BOUNDED_REGION, 5, 0}, {NodeKind::CONST, 0, 0},
        {NodeKind::CONST, 1, 0}, {NodeKind::CONST, 2, 0},
        {NodeKind::CONST, 2, 0}, {NodeKind::CONST, 2, 0},
        {NodeKind::CONST, 2, 0}, {NodeKind::CONST, 2, 0},
        {NodeKind::BLOCK_NIL, 0, 0},
    };
    ast.bounded_region_specs = {
        {3, plan, {}, phases(1)}, {6, plan, {}, phases(1)}};
  } else {
    ast.nodes = {
        {NodeKind::PROGRAM, 0, 0}, {NodeKind::BLOCK_CONS, 0, 0},
        {NodeKind::RETURN, 0, 0}, {NodeKind::BOUNDED_REGION, 5, 0},
        {NodeKind::BOUNDED_REGION, 5, 0}, {NodeKind::CONST, 0, 0},
        {NodeKind::CONST, 1, 0}, {NodeKind::CONST, 2, 0},
        {NodeKind::CONST, 2, 0}, {NodeKind::CONST, 2, 0},
        {NodeKind::CONST, 1, 0}, {NodeKind::CONST, 2, 0},
        {NodeKind::CONST, 2, 0}, {NodeKind::CONST, 2, 0},
        {NodeKind::BLOCK_NIL, 0, 0},
    };
    ast.bounded_region_specs = {
        {3, plan, {}, phases(1)}, {4, plan, {}, phases(1)}};
  }
  return ast;
}

const VerifiedScope& scope_at(const AstVerifyResult& result,
                              std::size_t node_index) {
  return result.verified.scopes[result.verified.expression_scope_ids[node_index]];
}

bool test_captures_and_exact_phase_scope() {
  AstProgram ast = name_capture_program();
  VerifyOptions options;
  options.capture_exact_scopes = true;
  AstVerifyResult result = verify_ast(ast, {{"x", RType::Int}}, options);
  if (!check(result.ok && result.verified.return_type == RType::Int,
             "input-name capture should type-check") ||
      !check(scope_at(result, 4).locals ==
                 std::vector<std::pair<int, RType>>{{0, RType::Int}},
             "initial state should retain the surrounding ordinary scope") ||
      !check(scope_at(result, 6).locals.empty() &&
                 scope_at(result, 6).binders ==
                     std::vector<std::pair<int, RType>>{{-11, RType::Int}},
             "phase should expose only its explicit Parameter binder")) {
    return false;
  }
  result = verify_ast(ast, {}, options);
  if (!check(result.ok,
             "unset ordinary-name capture should remain a typed lazy declaration") ||
      !check(scope_at(result, 3).locals ==
                 std::vector<std::pair<int, RType>>{{0, RType::Int}},
             "unset named capture should participate in the owner scope signature") ||
      !expect(ast, {{"x", RType::Float}}, VerifyCode::TypeMismatch,
              "wrong-type ordinary-name capture")) {
    return false;
  }
  result = verify_ast(assigned_name_capture_program(), {});
  if (!check(result.ok, "assigned ordinary-name capture should type-check")) return false;
  result = verify_ast(branch_only_name_capture_program(), {});
  if (!check(result.ok,
             "branch-only ordinary-name capture should preserve its possible unset state")) {
    return false;
  }

  ast = lexical_capture_program();
  result = verify_ast(ast, {}, options);
  if (!check(result.ok, "visible lexical capture should type-check") ||
      !check(scope_at(result, 5).binders ==
                 std::vector<std::pair<int, RType>>{{-8, RType::Int}},
             "bounded owner scope should retain its lexical capture declaration") ||
      !check(scope_at(result, 8).binders ==
                 std::vector<std::pair<int, RType>>{{-11, RType::Int}},
             "bounded phase scope should replace the outer lexical declaration")) {
    return false;
  }
  ast.bounded_region_specs[0].parameters[0].index = 8;
  if (!expect(ast, {}, VerifyCode::UndefinedBinder,
              "invisible lexical capture")) return false;
  ast = lexical_capture_program();
  ast.bounded_region_specs[0].plan.parameter_types[0] = ValueTag::Float;
  return expect(ast, {}, VerifyCode::TypeMismatch,
                "wrong-type lexical capture");
}

bool test_exact_children_and_closed_phases() {
  AstProgram ast = name_capture_program();
  ast.nodes[4] = {NodeKind::CONST, 1, 0};
  if (!expect(ast, {{"x", RType::Int}}, VerifyCode::TypeMismatch,
              "wrong initial state type")) return false;

  ast = dynamic_bound_program();
  if (!expect(ast, {}, VerifyCode::TypeMismatch,
              "non-Int dynamic bound")) return false;

  ast = name_capture_program();
  ast.nodes[6] = {NodeKind::VAR, 0, 0};
  ast.bounded_region_specs[0].phases[1].bindings.clear();
  if (!expect(ast, {{"x", RType::Int}}, VerifyCode::UndefinedLocal,
              "implicit ordinary access from phase")) return false;

  ast = lexical_capture_program();
  ast.nodes[8] = {NodeKind::REGION_VAR, 7, 0};
  ast.bounded_region_specs[0].phases[1].bindings.clear();
  if (!expect(ast, {}, VerifyCode::UndefinedBinder,
              "implicit outer lexical access from phase")) return false;

  ast = name_capture_program();
  ast.nodes[6] = {NodeKind::CONST, 1, 0};
  ast.bounded_region_specs[0].phases[1].bindings.clear();
  return expect(ast, {{"x", RType::Int}}, VerifyCode::TypeMismatch,
                "wrong phase result type");
}

bool test_nested_region_boundary() {
  AstVerifyResult result = verify_ast(nested_region(false), {});
  if (!check(result.ok,
             "bounded region should be allowed in an initial-state expression")) {
    return false;
  }
  return expect(nested_region(true), {}, VerifyCode::NestedIsolatedRegion,
                "bounded region nested in isolated phase");
}

}  // namespace

int main() {
  if (!test_captures_and_exact_phase_scope()) return 1;
  if (!test_exact_children_and_closed_phases()) return 1;
  if (!test_nested_region_boundary()) return 1;
  std::cout << "gagp_test_bounded_region_typing: OK\n";
  return 0;
}
