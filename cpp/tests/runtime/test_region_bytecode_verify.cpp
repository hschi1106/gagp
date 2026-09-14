#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <string>
#include <utility>

#include "gagp/core/bytecode_verify.hpp"

namespace {

using namespace gagp;

Instr op(Opcode opcode) { return Instr{opcode, 0, 0, false, false}; }
Instr op_a(Opcode opcode, int a) { return Instr{opcode, a, 0, true, false}; }

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

bool rejects(const BoundedRegionSegment& segment, int caller_n_locals,
             BytecodeVerifyCode code, const std::string& label,
             const BytecodeVerifyOptions& options = {}) {
  const BytecodeVerifyResult result =
      verify_bounded_region_segment(segment, caller_n_locals, options);
  return check(!result, label + " should fail") &&
         check(result.diagnostic.code == code,
               label + " expected " + bytecode_verify_code_name(code) +
                   " but got " +
                   bytecode_verify_code_name(result.diagnostic.code) + " at " +
                   result.diagnostic.path + ": " + result.diagnostic.message);
}

RegionBound literal(std::int64_t value) {
  RegionBound bound;
  bound.literal = value;
  return bound;
}

RegionBound operand(std::uint32_t index) {
  RegionBound bound;
  bound.kind = RegionBoundKind::Operand;
  bound.operand = index;
  return bound;
}

RegionStateTransition offset(std::int64_t amount) {
  RegionStateTransition transition;
  transition.kind = RegionTransitionKind::CoordinateOffset;
  transition.offset = amount;
  return transition;
}

RegionPhase constant_phase(const Value& value) {
  RegionPhase phase;
  phase.program.consts = {value};
  phase.program.code = {op_a(Opcode::PushConst, 0)};
  return phase;
}

RegionPhase load_phase(RegionSlotBank bank, std::uint32_t slot) {
  RegionPhase phase;
  phase.program.n_locals = 1;
  phase.program.code = {op_a(Opcode::Load, 0)};
  phase.bindings = {{RegionValueSlot{bank, slot}, 0}};
  return phase;
}

RegionPlan coordinate_plan() {
  RegionPlan plan;
  plan.state_types = {ValueTag::Int};
  plan.result_type = ValueTag::Int;
  plan.parameter_types = {ValueTag::Bool};
  plan.bound_operand_count = 1;
  plan.requests = {{{offset(-1)}}};
  plan.coordinate_slots = {0};
  plan.coordinate_rank = {{0, 1}};
  plan.coordinate_domains = {{literal(0), operand(1)}};
  return plan;
}

BoundedRegionSegment coordinate_segment() {
  BoundedRegionSegment segment;
  segment.plan = coordinate_plan();
  segment.parameter_locals = {1};
  segment.boundary = constant_phase(Value::from_int(0));
  segment.base_predicate = constant_phase(Value::from_bool(false));
  segment.base_body = load_phase(RegionSlotBank::State, 0);
  segment.combine = load_phase(RegionSlotBank::Result, 0);
  return segment;
}

BoundedRegionSegment sequence_segment() {
  BoundedRegionSegment segment;
  RegionPlan& plan = segment.plan;
  plan.state_types = {ValueTag::String};
  plan.result_type = ValueTag::Int;
  plan.preparations = {
      {ValueTag::Int, RegionPreparationKind::InteriorCut}};
  RegionStateTransition window;
  window.kind = RegionTransitionKind::SequenceWindow;
  window.window = {{WindowEndpointKind::Begin, 0},
                   {WindowEndpointKind::InteriorCut, 0}};
  plan.requests = {{{window}}};
  plan.progress = RegionProgressKind::SequenceWindows;
  plan.sequence_state = 0;
  segment.base_predicate = constant_phase(Value::from_bool(false));
  segment.base_body = constant_phase(Value::from_int(1));
  segment.preparations = {constant_phase(Value::from_int(1))};
  segment.combine = load_phase(RegionSlotBank::Result, 0);
  return segment;
}

BytecodeProgram program_with(BoundedRegionSegment segment,
                             bool enough_operands = true) {
  BytecodeProgram program;
  program.n_locals = 2;
  program.consts = {Value::from_int(4), Value::from_int(8)};
  program.code = {op_a(Opcode::PushConst, 0)};
  if (enough_operands) program.code.push_back(op_a(Opcode::PushConst, 1));
  program.code.push_back(op_a(Opcode::BoundedRegion, 0));
  program.code.push_back(op(Opcode::Return));
  program.bounded_region_segments.push_back(std::move(segment));
  return program;
}

RegionPhase mixed_output_phase() {
  RegionPhase phase;
  phase.program.n_locals = 1;
  phase.program.consts = {Value::from_bool(true), Value::from_int(1),
                          Value::from_bool(false)};
  phase.program.code = {
      op_a(Opcode::PushConst, 0), op_a(Opcode::JmpIfFalse, 5),
      op_a(Opcode::PushConst, 1), op_a(Opcode::Store, 0),
      op_a(Opcode::Jmp, 7),       op_a(Opcode::PushConst, 2),
      op_a(Opcode::Store, 0),     op_a(Opcode::Load, 0),
  };
  return phase;
}

RegionPhase maybe_initialized_phase(const Value& value) {
  RegionPhase phase;
  phase.program.n_locals = 1;
  phase.program.consts = {Value::from_bool(true), value};
  phase.program.code = {
      op_a(Opcode::PushConst, 0), op_a(Opcode::JmpIfFalse, 5),
      op_a(Opcode::PushConst, 1), op_a(Opcode::Store, 0),
      op_a(Opcode::Jmp, 6),       op_a(Opcode::Jmp, 6),
      op_a(Opcode::Load, 0),
  };
  return phase;
}

RegionPhase numeric_binary_phase(Opcode opcode, const Value& lhs,
                                 const Value& rhs) {
  RegionPhase phase;
  phase.program.consts = {lhs, rhs};
  phase.program.code = {op_a(Opcode::PushConst, 0),
                        op_a(Opcode::PushConst, 1), op(opcode)};
  return phase;
}

RegionPhase jump_to_division_phase() {
  RegionPhase phase;
  phase.program.consts = {Value::from_int(4), Value::from_int(2),
                          Value::from_int(0)};
  phase.program.code = {
      op_a(Opcode::PushConst, 0), op_a(Opcode::PushConst, 1),
      op_a(Opcode::Jmp, 4),       op_a(Opcode::PushConst, 2),
      op(Opcode::Div),
  };
  return phase;
}

RegionPhase conditional_jump_to_division_phase(Opcode jump_opcode) {
  RegionPhase phase;
  phase.program.n_locals = 1;
  phase.program.consts = {Value::from_int(4), Value::from_int(2),
                          Value::from_bool(true), Value::from_int(0)};
  phase.program.code = {
      op_a(Opcode::PushConst, 0), op_a(Opcode::PushConst, 1),
      op_a(Opcode::PushConst, 2), op_a(jump_opcode, 6),
      op_a(Opcode::Store, 0),     op_a(Opcode::PushConst, 3),
      op(Opcode::Div),
  };
  return phase;
}

RegionPhase jump_to_zero_push_phase() {
  RegionPhase phase;
  phase.program.consts = {Value::from_int(4), Value::from_int(2),
                          Value::from_int(0)};
  phase.program.code = {
      op_a(Opcode::PushConst, 0), op_a(Opcode::Jmp, 3),
      op_a(Opcode::PushConst, 1), op_a(Opcode::PushConst, 2),
      op(Opcode::Div),
  };
  return phase;
}

PhaseProgram legacy_load(std::initializer_list<int> names, int selected) {
  PhaseProgram phase;
  phase.n_locals = static_cast<int>(names.size());
  int local = 0;
  for (int name : names) phase.binder_locals[name] = local++;
  phase.code = {op_a(Opcode::Load, selected)};
  return phase;
}

BytecodeProgram legacy_phase_with_bounded_call() {
  BytecodeProgram program;
  program.consts = {Value::from_int(0)};
  program.code = {op_a(Opcode::PushConst, 0), op(Opcode::Return)};
  AsgpDcSegment segment;
  segment.solve_xs_name = 0;
  segment.solve_n_name = 1;
  segment.solve_lo_name = 2;
  segment.divide_n_name = 3;
  segment.combine_left_name = 4;
  segment.combine_right_name = 5;
  segment.solve = legacy_load({0, 1, 2}, 0);
  segment.solve.code = {op_a(Opcode::BoundedRegion, 0)};
  segment.divide = legacy_load({3}, 0);
  segment.combine = legacy_load({4, 5}, 0);
  program.asgp_dc_segments.push_back(std::move(segment));
  return program;
}

}  // namespace

int main() {
  using namespace gagp;

  BoundedRegionSegment segment = coordinate_segment();
  BytecodeVerifyResult result = verify_bounded_region_segment(segment, 2);
  if (!check(result.ok && result.verified.phase_program_count == 4 &&
                 result.verified.max_stack_depth == 1,
             "valid coordinate segment verifies with exact phase accounting")) {
    return 1;
  }

  BytecodeProgram program = program_with(segment);
  result = verify_bytecode(program);
  if (!check(result.ok && result.verified.phase_program_count == 4 &&
                 result.verified.max_stack_depth == 2,
             "bounded-region opcode consumes state and bound operands")) {
    return 1;
  }

  result = verify_bounded_region_segment(sequence_segment(), 0);
  if (!check(result.ok && result.verified.phase_program_count == 4,
             "valid sequence segment verifies without boundary")) return 1;

  segment = coordinate_segment();
  segment.plan.requests.clear();
  if (!rejects(segment, 2, BytecodeVerifyCode::InvalidSegmentMetadata,
               "invalid plan")) return 1;

  segment = coordinate_segment();
  segment.parameter_locals.clear();
  if (!rejects(segment, 2, BytecodeVerifyCode::InvalidSegmentMetadata,
               "parameter count")) return 1;
  segment = coordinate_segment();
  segment.parameter_locals[0] = 2;
  if (!rejects(segment, 2, BytecodeVerifyCode::InvalidLocalIndex,
               "caller parameter range")) return 1;
  segment = coordinate_segment();
  segment.plan.parameter_types.push_back(ValueTag::Int);
  segment.parameter_locals = {1, 1};
  if (!rejects(segment, 2, BytecodeVerifyCode::InvalidSegmentMetadata,
               "duplicate caller parameters")) return 1;

  segment = coordinate_segment();
  segment.boundary.reset();
  if (!rejects(segment, 2, BytecodeVerifyCode::InvalidSegmentMetadata,
               "missing coordinate boundary")) return 1;
  segment = sequence_segment();
  segment.boundary = constant_phase(Value::from_int(0));
  if (!rejects(segment, 0, BytecodeVerifyCode::InvalidSegmentMetadata,
               "sequence boundary")) return 1;
  segment = sequence_segment();
  segment.preparations.clear();
  if (!rejects(segment, 0, BytecodeVerifyCode::InvalidSegmentMetadata,
               "preparation count")) return 1;
  segment = coordinate_segment();
  segment.plan.request_expression_types = {ValueTag::Int};
  if (!rejects(segment, 2, BytecodeVerifyCode::InvalidSegmentMetadata,
               "request expression count")) return 1;

  segment = coordinate_segment();
  segment.base_predicate = constant_phase(Value::from_int(1));
  if (!rejects(segment, 2, BytecodeVerifyCode::InvalidFallthrough,
               "wrong exact predicate type")) return 1;
  segment = coordinate_segment();
  segment.base_body = mixed_output_phase();
  if (!rejects(segment, 2, BytecodeVerifyCode::InvalidFallthrough,
               "mixed successful output type")) return 1;
  segment.base_body.program.code.push_back(op(Opcode::CheckInt));
  if (!check(verify_bounded_region_segment(segment, 2).ok,
             "runtime type guard proves an exact successful output")) return 1;

  segment = coordinate_segment();
  segment.base_body = maybe_initialized_phase(Value::from_int(3));
  if (!check(verify_bounded_region_segment(segment, 2).ok,
             "unset branch does not erase the successful branch type")) return 1;
  segment.base_body = maybe_initialized_phase(Value::from_bool(false));
  if (!rejects(segment, 2, BytecodeVerifyCode::InvalidFallthrough,
               "unset branch does not hide a wrong successful type")) return 1;

  segment = coordinate_segment();
  segment.base_body = numeric_binary_phase(
      Opcode::Div, Value::from_int(7), Value::from_int(0));
  if (!check(verify_bounded_region_segment(segment, 2).ok,
             "integer literal zero division is an error-only phase")) return 1;
  segment.base_body = numeric_binary_phase(
      Opcode::Mod, Value::from_float(7.0), Value::from_float(0.0));
  if (!check(verify_bounded_region_segment(segment, 2).ok,
             "float literal zero modulo is an error-only phase")) return 1;
  segment.base_body = numeric_binary_phase(
      Opcode::Div, Value::from_float(7.0), Value::from_float(-0.0));
  if (!check(verify_bounded_region_segment(segment, 2).ok,
             "negative float zero division is an error-only phase")) return 1;

  segment.base_body = numeric_binary_phase(
      Opcode::Div, Value::from_int(8), Value::from_int(2));
  if (!rejects(segment, 2, BytecodeVerifyCode::InvalidFallthrough,
               "successful integer division has a Float result")) return 1;

  segment.base_body = jump_to_division_phase();
  if (!rejects(segment, 2, BytecodeVerifyCode::InvalidFallthrough,
               "jump entering division bypasses adjacent zero proof")) return 1;
  segment.base_body = conditional_jump_to_division_phase(Opcode::JmpIfTrue);
  if (!rejects(segment, 2, BytecodeVerifyCode::InvalidFallthrough,
               "true branch entering division bypasses adjacent zero proof")) {
    return 1;
  }
  segment.base_body = conditional_jump_to_division_phase(Opcode::JmpIfFalse);
  if (!rejects(segment, 2, BytecodeVerifyCode::InvalidFallthrough,
               "false branch entering division bypasses adjacent zero proof")) {
    return 1;
  }
  segment.base_body = jump_to_zero_push_phase();
  if (!check(verify_bounded_region_segment(segment, 2).ok,
             "jump entering adjacent zero push preserves error-only proof")) return 1;

  segment = coordinate_segment();
  segment.base_body.program.n_locals = 1;
  segment.base_body.program.consts.clear();
  segment.base_body.program.code = {op_a(Opcode::Load, 0)};
  segment.base_body.bindings.clear();
  if (!check(verify_bounded_region_segment(segment, 2).ok,
             "guaranteed Name-error-only phase remains valid")) return 1;

  segment = coordinate_segment();
  segment.base_body.program.var2idx["x"] = 0;
  if (!rejects(segment, 2, BytecodeVerifyCode::InvalidVarMapping,
               "generic variable mapping")) return 1;
  segment = coordinate_segment();
  segment.base_body.program.binder_locals[3] = 0;
  if (!rejects(segment, 2, BytecodeVerifyCode::InvalidBinderLocal,
               "generic binder mapping")) return 1;
  segment = coordinate_segment();
  segment.base_body.bindings.push_back(
      {RegionValueSlot{RegionSlotBank::State, 0}, 0});
  if (!rejects(segment, 2, BytecodeVerifyCode::InvalidSegmentMetadata,
               "duplicate source binding")) return 1;
  segment = coordinate_segment();
  segment.base_body.bindings.push_back(
      {RegionValueSlot{RegionSlotBank::Parameter, 0}, 0});
  if (!rejects(segment, 2, BytecodeVerifyCode::InvalidBinderLocal,
               "duplicate destination binding")) return 1;
  segment = coordinate_segment();
  segment.base_body.bindings[0].source =
      RegionValueSlot{RegionSlotBank::Result, 0};
  if (!rejects(segment, 2, BytecodeVerifyCode::InvalidSegmentMetadata,
               "phase-invisible slot")) return 1;

  segment = coordinate_segment();
  segment.base_body.program.code = {op_a(Opcode::AsgpDp1d, 0)};
  if (!rejects(segment, 2, BytecodeVerifyCode::InvalidPrivateOpcode,
               "nested legacy structured opcode")) return 1;
  segment = coordinate_segment();
  segment.base_body.program.code = {op_a(Opcode::BoundedRegion, 0)};
  if (!rejects(segment, 2, BytecodeVerifyCode::InvalidPrivateOpcode,
               "nested generic structured opcode")) return 1;
  program = legacy_phase_with_bounded_call();
  result = verify_bytecode(program);
  if (!check(!result &&
                 result.diagnostic.code ==
                     BytecodeVerifyCode::InvalidPrivateOpcode,
             "generic structured opcode is forbidden in legacy phases")) {
    return 1;
  }

  segment = coordinate_segment();
  segment.base_body.program.instruction_fuel = {1, 1};
  if (!rejects(segment, 2, BytecodeVerifyCode::InvalidFuelSchedule,
               "phase fuel schedule")) return 1;
  BytecodeVerifyOptions limited;
  segment = coordinate_segment();
  limited.max_constants_per_code = 1;
  segment.base_predicate.program.consts.push_back(Value::from_bool(true));
  if (!rejects(segment, 2, BytecodeVerifyCode::ResourceLimit,
               "phase resource profile", limited)) return 1;

  program = program_with(coordinate_segment(), false);
  const BytecodeVerifyResult underflow = verify_bytecode(program);
  if (!check(!underflow &&
                 underflow.diagnostic.code == BytecodeVerifyCode::StackUnderflow,
             "bounded-region opcode checks full invocation arity")) return 1;

  program = program_with(coordinate_segment());
  program.code[2].has_a = false;
  result = verify_bytecode(program);
  if (!check(!result &&
                 result.diagnostic.code == BytecodeVerifyCode::MissingOperand,
             "bounded-region opcode requires its segment operand")) return 1;
  program = program_with(coordinate_segment());
  program.bounded_region_segments.clear();
  result = verify_bytecode(program);
  if (!check(!result &&
                 result.diagnostic.code ==
                     BytecodeVerifyCode::InvalidSegmentIndex,
             "bounded-region opcode validates its segment index")) return 1;

  program = program_with(coordinate_segment());
  BytecodeVerifyOptions public_only;
  public_only.allow_private_opcodes = false;
  result = verify_bytecode(program, public_only);
  if (!check(!result &&
                 result.diagnostic.code ==
                     BytecodeVerifyCode::InvalidPrivateOpcode,
             "bounded-region opcode obeys private opcode profile")) return 1;

  return 0;
}
