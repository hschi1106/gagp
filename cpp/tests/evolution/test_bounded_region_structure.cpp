#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/bounded_region.hpp"

namespace {

using namespace gagp;
using namespace gagp::evo;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

bool rejects(const AstProgram& ast, VerifyCode code, const std::string& label) {
  const AstVerifyResult result = verify_ast_structure(ast);
  return check(!result, label + " should fail") &&
         check(result.diagnostic.code == code,
               label + " expected " + verify_code_name(code) + " but got " +
                   verify_code_name(result.diagnostic.code) + " at " +
                   result.diagnostic.path + ": " + result.diagnostic.message);
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

RegionPlan coordinate_plan() {
  RegionPlan plan;
  plan.state_types = {ValueTag::Int};
  plan.result_type = ValueTag::Int;
  plan.requests = {{{offset(-1)}}};
  plan.coordinate_slots = {0};
  plan.coordinate_rank = {{0, 1}};
  plan.coordinate_domains = {{literal(0), literal(3)}};
  return plan;
}

RegionPlan sequence_plan() {
  RegionPlan plan;
  plan.state_types = {ValueTag::String};
  plan.result_type = ValueTag::String;
  plan.preparations = {
      {ValueTag::Int, RegionPreparationKind::InteriorCut}};
  RegionStateTransition transition;
  transition.kind = RegionTransitionKind::SequenceWindow;
  transition.window = {{WindowEndpointKind::Begin, 0},
                       {WindowEndpointKind::InteriorCut, 0}};
  plan.requests = {{{transition}}};
  plan.progress = RegionProgressKind::SequenceWindows;
  return plan;
}

AstProgram coordinate_ast() {
  AstProgram ast;
  ast.consts = {Value::from_int(2), Value::from_bool(false),
                Value::from_int(1), Value::from_int(2), Value::from_int(0)};
  ast.nodes = {
      {NodeKind::PROGRAM},
      {NodeKind::BLOCK_CONS},
      {NodeKind::RETURN},
      {NodeKind::BOUNDED_REGION, 5, 0},
      {NodeKind::CONST, 0, 0},
      {NodeKind::CONST, 1, 0},
      {NodeKind::CONST, 2, 0},
      {NodeKind::CONST, 3, 0},
      {NodeKind::CONST, 4, 0},
      {NodeKind::BLOCK_NIL},
  };
  BoundedRegionSpec spec;
  spec.node_index = 3;
  spec.plan = coordinate_plan();
  spec.phases = {{1, {}}, {2, {}}, {3, {}}, {4, {}}};
  ast.bounded_region_specs = {spec};
  return ast;
}

AstProgram coordinate_in_let_ast() {
  AstProgram ast = coordinate_ast();
  ast.nodes.insert(ast.nodes.begin() + 3, {NodeKind::LET_REGION, 0, 0});
  ast.nodes.insert(ast.nodes.begin() + 4, {NodeKind::CONST, 0, 0});
  ast.bounded_region_specs[0].node_index = 5;
  ast.bounded_region_specs[0].plan.parameter_types = {ValueTag::Int};
  ast.bounded_region_specs[0].parameters = {{RegionCaptureKind::Lexical, 20}};
  ast.lexical_regions = {{3, 1, {{20, RType::Int}}}};
  return ast;
}

}  // namespace

int main() {
  using namespace gagp;
  using namespace gagp::evo;

  const RegionPlan coordinate = coordinate_plan();
  if (!check(bounded_region_arity(coordinate) == 5,
             "coordinate flat arity is canonical") ||
      !check(bounded_region_phase_kind(coordinate, 0) ==
                 RegionPhaseKind::BasePredicate &&
                 bounded_region_phase_kind(coordinate, 1) ==
                     RegionPhaseKind::BaseBody &&
                 bounded_region_phase_kind(coordinate, 2) ==
                     RegionPhaseKind::Combine &&
                 bounded_region_phase_kind(coordinate, 3) ==
                     RegionPhaseKind::Boundary,
             "coordinate phase order is canonical") ||
      !check(bounded_region_phase_type(coordinate, 0) == ValueTag::Bool &&
                 bounded_region_phase_type(coordinate, 3) == ValueTag::Int,
             "coordinate phase types are exact")) {
    return 1;
  }

  const RegionPlan sequence = sequence_plan();
  if (!check(bounded_region_arity(sequence) == 5 &&
                 bounded_region_phase_kind(sequence, 2) ==
                     RegionPhaseKind::Preparation &&
                 bounded_region_preparation_ordinal(sequence, 2) == 0 &&
                 bounded_region_phase_kind(sequence, 3) ==
                     RegionPhaseKind::Combine &&
                 bounded_region_phase_type(sequence, 2) == ValueTag::Int,
             "sequence phase layout omits boundary and exposes preparation")) {
    return 1;
  }
  bool ordinal_rejected = false;
  try {
    (void)bounded_region_phase_kind(coordinate, 4);
  } catch (const std::invalid_argument&) {
    ordinal_rejected = true;
  }
  if (!check(ordinal_rejected, "out-of-range phase ordinal is rejected")) return 1;

  AstProgram ast = coordinate_ast();
  AstVerifyResult result = verify_ast_structure(ast);
  if (!check(result.ok, "valid bounded region structure verifies: " +
                            result.diagnostic.message) ||
      !check(lookup_bounded_region_spec(ast, 3) ==
                 &ast.bounded_region_specs[0] &&
                 lookup_bounded_region_spec(ast, 2) == nullptr,
             "bounded region spec lookup preserves the owning row")) {
    return 1;
  }

  ast = coordinate_ast();
  ast.bounded_region_specs.clear();
  if (!rejects(ast, VerifyCode::MissingMetadata, "missing bounded metadata"))
    return 1;
  ast = coordinate_ast();
  ast.bounded_region_specs.push_back(ast.bounded_region_specs[0]);
  if (!rejects(ast, VerifyCode::DuplicateMetadata, "duplicate bounded metadata"))
    return 1;
  ast = coordinate_ast();
  ast.bounded_region_specs[0].node_index = 2;
  if (!rejects(ast, VerifyCode::MetadataNodeMismatch,
               "bounded metadata owner")) return 1;

  ast = coordinate_ast();
  ast.nodes[3].i1 = 1;
  if (!rejects(ast, VerifyCode::InvalidIndexField, "bounded unused i1"))
    return 1;
  ast = coordinate_ast();
  ast.nodes[3].i0 = 53;
  if (!rejects(ast, VerifyCode::InvalidIndexField, "bounded arity capacity"))
    return 1;
  ast = coordinate_ast();
  ast.nodes[3].i0 = 4;
  ast.nodes.erase(ast.nodes.begin() + 8);
  if (!rejects(ast, VerifyCode::DependencyArityMismatch,
               "bounded plan arity mismatch")) return 1;

  ast = coordinate_ast();
  ast.bounded_region_specs[0].plan.requests.clear();
  if (!rejects(ast, VerifyCode::InvalidBounds, "invalid bounded plan"))
    return 1;
  ast = coordinate_ast();
  ast.bounded_region_specs[0].plan.parameter_types = {ValueTag::Int};
  if (!rejects(ast, VerifyCode::DependencyArityMismatch,
               "capture count")) return 1;
  ast.bounded_region_specs[0].parameters = {
      {RegionCaptureKind::Name, 0}};
  ast.names.clear();
  if (!rejects(ast, VerifyCode::NameIndexOutOfRange, "name capture"))
    return 1;
  ast = coordinate_ast();
  ast.bounded_region_specs[0].plan.parameter_types = {ValueTag::Int};
  ast.bounded_region_specs[0].parameters = {
      {RegionCaptureKind::Lexical, std::numeric_limits<int>::max()}};
  if (!rejects(ast, VerifyCode::InvalidIndexField, "lexical capture id"))
    return 1;

  ast = coordinate_ast();
  ast.bounded_region_specs[0].phases.pop_back();
  if (!rejects(ast, VerifyCode::DependencyArityMismatch, "phase count"))
    return 1;
  ast = coordinate_ast();
  ast.bounded_region_specs[0].phases[1].argument = 1;
  if (!rejects(ast, VerifyCode::MetadataNodeMismatch,
               "phase argument order")) return 1;

  ast = coordinate_ast();
  ast.bounded_region_specs[0].phases[0].bindings = {
      {{RegionSlotBank::Result, 0}, 20}};
  if (!rejects(ast, VerifyCode::MetadataNodeMismatch,
               "phase source visibility")) return 1;
  ast = coordinate_ast();
  ast.bounded_region_specs[0].phases[0].bindings = {
      {{RegionSlotBank::State, 0}, 20},
      {{RegionSlotBank::State, 0}, 21}};
  if (!rejects(ast, VerifyCode::DuplicateMetadata,
               "duplicate phase source")) return 1;
  ast = coordinate_ast();
  ast.bounded_region_specs[0].phases[0].bindings = {
      {{RegionSlotBank::State, 0}, 20}};
  ast.bounded_region_specs[0].phases[1].bindings = {
      {{RegionSlotBank::State, 0}, 20}};
  if (!rejects(ast, VerifyCode::DuplicateBinder,
               "globally duplicate phase binder")) return 1;
  ast = coordinate_in_let_ast();
  result = verify_ast_structure(ast);
  if (!check(result.ok, "enclosing lexical capture verifies")) return 1;
  ast.bounded_region_specs[0].phases[0].bindings = {
      {{RegionSlotBank::State, 0}, 20}};
  if (!rejects(ast, VerifyCode::DuplicateBinder,
               "phase binder collides with lexical declaration")) return 1;
  ast = coordinate_ast();
  ast.bounded_region_specs[0].phases[0].bindings = {
      {{RegionSlotBank::State, 0}, std::numeric_limits<int>::max()}};
  if (!rejects(ast, VerifyCode::InvalidIndexField, "phase binder id"))
    return 1;

  ast = coordinate_ast();
  ast.names = {"capture"};
  ast.bounded_region_specs[0].plan.parameter_types = {ValueTag::Int, ValueTag::Int};
  ast.bounded_region_specs[0].parameters = {{RegionCaptureKind::Name, 0}, {RegionCaptureKind::Name, 0}};
  if (!rejects(ast, VerifyCode::DuplicateMetadata, "duplicate captures")) return 1;
  VerifyOptions plan_limited;
  plan_limited.max_metadata_entries = 5;
  if (!check(!verify_ast_structure(coordinate_ast(), plan_limited),
             "plan vectors must count beyond the owner and four phase rows")) return 1;

  VerifyOptions limited;
  limited.max_metadata_entries = 4;
  result = verify_ast_structure(coordinate_ast(), limited);
  if (!check(!result && result.diagnostic.code == VerifyCode::ResourceLimit,
             "bounded nested metadata participates in resource limits")) {
    return 1;
  }

  return 0;
}
