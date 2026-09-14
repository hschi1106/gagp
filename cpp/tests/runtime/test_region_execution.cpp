#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/core/builtin.hpp"
#include "gagp/core/bytecode.hpp"
#include "gagp/core/bytecode_verify.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace {

using namespace gagp;

Instr ins(Opcode op) { return Instr{op, 0, 0, false, false}; }
Instr ins_a(Opcode op, int a) { return Instr{op, a, 0, true, false}; }
Instr ins_ab(Opcode op, int a, int b) { return Instr{op, a, b, true, true}; }

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << '\n';
  return condition;
}

bool is_error(const ExecResult& result, ErrCode code) {
  return result.is_error && result.err.code == code;
}

bool exact_value(const Value& left, const Value& right) {
  if (left.tag != right.tag) return false;
  switch (left.tag) {
    case ValueTag::Int:
    case ValueTag::Char:
    case ValueTag::FallbackToken:
      return left.i == right.i;
    case ValueTag::Float:
      return left.f == right.f;
    case ValueTag::Bool:
      return left.b == right.b;
    case ValueTag::String: {
      std::string a;
      std::string b;
      return payload::lookup_string(left, &a) && payload::lookup_string(right, &b) && a == b;
    }
    case ValueTag::IntList:
    case ValueTag::FloatList:
    case ValueTag::StringList: {
      std::vector<Value> a;
      std::vector<Value> b;
      if (!payload::lookup_list(left, &a) || !payload::lookup_list(right, &b) ||
          a.size() != b.size()) return false;
      for (std::size_t i = 0; i < a.size(); ++i)
        if (!exact_value(a[i], b[i])) return false;
      return true;
    }
    case ValueTag::Invalid:
      return true;
  }
  return false;
}

RegionValueSlot slot(RegionSlotBank bank, std::uint32_t index) {
  return RegionValueSlot{bank, index};
}

RegionPhase phase(std::vector<Value> constants, std::vector<Instr> code,
                  std::vector<RegionPhaseBinding> bindings = {},
                  int locals = 0, bool zero_fuel = true) {
  RegionPhase result;
  result.program.consts = std::move(constants);
  result.program.code = std::move(code);
  result.program.n_locals = locals;
  if (zero_fuel)
    result.program.instruction_fuel.assign(result.program.code.size(), 0);
  result.bindings = std::move(bindings);
  return result;
}

RegionPhase constant_phase(Value result) {
  return phase({result}, {ins_a(Opcode::PushConst, 0), ins(Opcode::Return)});
}

RegionPhase load_phase(RegionSlotBank bank, std::uint32_t source) {
  return phase({}, {ins_a(Opcode::Load, 0), ins(Opcode::Return)},
               {{slot(bank, source), 0}}, 1);
}

RegionPhase zero_div_phase(ValueTag output) {
  std::vector<Instr> code{
      ins_a(Opcode::PushConst, 0), ins_a(Opcode::PushConst, 1),
      ins(Opcode::Mod)};
  std::vector<Value> constants{Value::from_int(1), Value::from_int(0)};
  if (output == ValueTag::Bool) {
    code.push_back(ins_a(Opcode::PushConst, 1));
    code.push_back(ins(Opcode::Eq));
  }
  code.push_back(ins(Opcode::Return));
  return phase(std::move(constants), std::move(code));
}

RegionPhase coordinate_base_predicate_1d() {
  return phase({Value::from_int(0)},
               {ins_a(Opcode::Load, 0), ins_a(Opcode::PushConst, 0),
                ins(Opcode::Eq), ins(Opcode::Return)},
               {{slot(RegionSlotBank::State, 0), 0}}, 1);
}

RegionPhase coordinate_base_predicate_2d() {
  return phase(
      {Value::from_int(0), Value::from_bool(false)},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::PushConst, 0), ins(Opcode::Eq),
       ins_a(Opcode::JmpIfFalse, 8), ins_a(Opcode::Load, 1),
       ins_a(Opcode::PushConst, 0), ins(Opcode::Eq), ins(Opcode::Return),
       ins_a(Opcode::PushConst, 1), ins(Opcode::Return)},
      {{slot(RegionSlotBank::State, 0), 0},
       {slot(RegionSlotBank::State, 1), 1}}, 2);
}

RegionStateTransition offset(std::uint32_t source, std::int64_t amount) {
  RegionStateTransition transition;
  transition.kind = RegionTransitionKind::CoordinateOffset;
  transition.source_state = source;
  transition.offset = amount;
  return transition;
}

RegionStateTransition window(std::uint32_t source, WindowEndpoint begin,
                             WindowEndpoint end) {
  RegionStateTransition transition;
  transition.kind = RegionTransitionKind::SequenceWindow;
  transition.source_state = source;
  transition.window = SequenceWindow{begin, end};
  return transition;
}

RegionBound literal_bound(std::int64_t value) {
  RegionBound result;
  result.kind = RegionBoundKind::Literal;
  result.literal = value;
  return result;
}

RegionBound operand_bound(std::uint32_t operand) {
  RegionBound result;
  result.kind = RegionBoundKind::Operand;
  result.operand = operand;
  return result;
}

RegionPhase combine_sum_2d() {
  return phase(
      {Value::from_int(1)},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::Load, 1), ins(Opcode::Add),
       ins_a(Opcode::PushConst, 0), ins(Opcode::Add), ins(Opcode::Return)},
      {{slot(RegionSlotBank::Result, 0), 0},
       {slot(RegionSlotBank::Result, 1), 1}}, 2);
}

BoundedRegionSegment coordinate_2d_segment(bool memoized = true) {
  BoundedRegionSegment segment;
  RegionPlan& plan = segment.plan;
  plan.state_types = {ValueTag::Int, ValueTag::Int};
  plan.result_type = ValueTag::Int;
  plan.bound_operand_count = 2;
  plan.requests = {{{offset(0, -1), offset(1, 1)}},
                   {{offset(0, 0), offset(1, -1)}}};
  plan.limits = {64, memoized ? 64U : 0U, 1};
  plan.memoized = memoized;
  plan.progress = RegionProgressKind::Coordinates;
  plan.coordinate_slots = {0, 1};
  plan.coordinate_rank = {{0, 1}, {1, 1}};
  plan.coordinate_domains = {
      {literal_bound(0), operand_bound(2)},
      {literal_bound(0), operand_bound(3)}};
  plan.coordinate_endpoint = DomainEndpoint::Inclusive;
  segment.boundary = constant_phase(Value::from_int(0));
  segment.base_predicate = coordinate_base_predicate_2d();
  segment.base_body = constant_phase(Value::from_int(1));
  segment.combine = combine_sum_2d();
  return segment;
}

BoundedRegionSegment typed_unary_segment(Value terminal_value,
                                         bool duplicate = true) {
  BoundedRegionSegment segment;
  RegionPlan& plan = segment.plan;
  plan.state_types = {ValueTag::Int};
  plan.result_type = terminal_value.tag;
  plan.requests = duplicate
      ? std::vector<RegionRequest>{{{offset(0, -1)}}, {{offset(0, -1)}}}
      : std::vector<RegionRequest>{{{offset(0, -1)}}};
  plan.limits = {16, 16, 1};
  plan.memoized = true;
  plan.duplicate_policy = duplicate ? DuplicatePolicy::Allow : DuplicatePolicy::Reject;
  plan.progress = RegionProgressKind::Coordinates;
  plan.coordinate_slots = {0};
  plan.coordinate_rank = {{0, 1}};
  plan.coordinate_domains = {{literal_bound(0), literal_bound(3)}};
  plan.coordinate_endpoint = DomainEndpoint::Inclusive;
  segment.boundary = constant_phase(terminal_value);
  segment.base_predicate = coordinate_base_predicate_1d();
  segment.base_body = constant_phase(terminal_value);
  segment.combine = load_phase(RegionSlotBank::Result, 0);
  return segment;
}

BytecodeProgram invocation(BoundedRegionSegment segment,
                           std::vector<Value> operands,
                           int caller_locals = 0,
                           bool isolate_fuel = true) {
  BytecodeProgram program;
  program.consts = std::move(operands);
  for (std::size_t i = 0; i < program.consts.size(); ++i)
    program.code.push_back(ins_a(Opcode::PushConst, static_cast<int>(i)));
  program.code.push_back(ins_a(Opcode::BoundedRegion, 0));
  program.code.push_back(ins(Opcode::Return));
  program.n_locals = caller_locals;
  program.bounded_region_segments.push_back(std::move(segment));
  if (isolate_fuel) {
    program.instruction_fuel.assign(program.code.size(), 0);
    program.instruction_fuel[program.consts.size()] = 1;
  }
  return program;
}

bool valid(const BytecodeProgram& program, const std::string& label) {
  const BytecodeVerifyResult verified = verify_bytecode(program);
  return check(verified.ok, label + ": " + verified.diagnostic.message);
}

bool test_custom_coordinate_execution() {
  for (const bool memoized : {false, true}) {
    const BytecodeProgram program = invocation(
        coordinate_2d_segment(memoized),
        {Value::from_int(2), Value::from_int(2),
         Value::from_int(2), Value::from_int(4)});
    if (!valid(program, "custom coordinate program")) return false;
    const ExecResult result = execute_bytecode_cpu(program, {}, 1000);
    if (!check(!result.is_error && result.value.tag == ValueTag::Int &&
                   result.value.i == 40,
               "custom coordinate recurrence returned the wrong value")) {
      return false;
    }
  }
  return true;
}

Value sample(ValueTag tag) {
  switch (tag) {
    case ValueTag::Int: return Value::from_int(-7);
    case ValueTag::Float: return Value::from_float(-1.25);
    case ValueTag::Bool: return Value::from_bool(true);
    case ValueTag::Char: return Value::from_char(0x1f600);
    case ValueTag::String: return payload::make_string_value(std::string("a\0", 2));
    case ValueTag::IntList:
      return payload::make_int_list_value({Value::from_int(2), Value::from_int(3)});
    case ValueTag::FloatList:
      return payload::make_float_list_value({Value::from_float(2.5)});
    case ValueTag::StringList:
      return payload::make_string_list_value({payload::make_string_value("x")});
    case ValueTag::FallbackToken:
    case ValueTag::Invalid: break;
  }
  return Value::invalid();
}

bool test_all_exact_result_tags_and_duplicate_memo() {
  for (const ValueTag tag : {ValueTag::Int, ValueTag::Float, ValueTag::Bool,
                             ValueTag::Char, ValueTag::String, ValueTag::IntList,
                             ValueTag::FloatList, ValueTag::StringList}) {
    const Value expected = sample(tag);
    const BytecodeProgram program = invocation(
        typed_unary_segment(expected), {Value::from_int(2)});
    if (!valid(program, "typed result program")) return false;
    const ExecResult result = execute_bytecode_cpu(program, {}, 100);
    if (!check(!result.is_error && result.value.tag == tag &&
                   exact_value(result.value, expected),
               "bounded region changed an exact result tag or payload")) {
      return false;
    }
  }
  const Value expected = Value::from_int(9);
  const BytecodeProgram duplicate = invocation(
      typed_unary_segment(expected), {Value::from_int(2)});
  return check(is_error(execute_bytecode_cpu(duplicate, {}, 5), ErrCode::Timeout),
               "base reevaluation and charged memo hit used too little fuel") &&
         check(!execute_bytecode_cpu(duplicate, {}, 6).is_error,
               "duplicate recurrence failed at opcode plus five entry charges");
}

RegionPhase false_phase() { return constant_phase(Value::from_bool(false)); }

RegionPhase cut_phase(bool second) {
  if (!second) {
    return phase(
        {Value::from_int(3)},
        {ins_a(Opcode::Load, 0), ins_a(Opcode::PushConst, 0),
         ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::IDiv0), 2),
         ins(Opcode::Return)},
        {{slot(RegionSlotBank::Measure, 0), 0}}, 1);
  }
  return phase(
      {Value::from_int(2)},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::PushConst, 0),
       ins(Opcode::Mul), ins(Opcode::Return)},
      {{slot(RegionSlotBank::Prepared, 0), 0}}, 1);
}

RegionPhase concat_three_phase() {
  return phase(
      {},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::Load, 1),
       ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Concat), 2),
       ins_a(Opcode::Load, 2),
       ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Concat), 2),
       ins(Opcode::Return)},
      {{slot(RegionSlotBank::Result, 0), 0},
       {slot(RegionSlotBank::Result, 1), 1},
       {slot(RegionSlotBank::Result, 2), 2}}, 3);
}

WindowEndpoint endpoint(WindowEndpointKind kind, std::uint32_t cut = 0) {
  return WindowEndpoint{kind, cut};
}

BoundedRegionSegment sequence_segment(ValueTag type) {
  BoundedRegionSegment segment;
  RegionPlan& plan = segment.plan;
  plan.state_types = {type};
  plan.result_type = type;
  plan.preparations = {
      {ValueTag::Int, RegionPreparationKind::InteriorCut},
      {ValueTag::Int, RegionPreparationKind::InteriorCut}};
  const auto begin = endpoint(WindowEndpointKind::Begin);
  const auto cut0 = endpoint(WindowEndpointKind::InteriorCut, 0);
  const auto cut1 = endpoint(WindowEndpointKind::InteriorCut, 1);
  const auto end = endpoint(WindowEndpointKind::End);
  plan.requests = {{{window(0, begin, cut0)}},
                   {{window(0, cut0, cut1)}},
                   {{window(0, cut1, end)}}};
  plan.limits = {16, 0, 1};
  plan.progress = RegionProgressKind::SequenceWindows;
  plan.sequence_state = 0;
  segment.base_predicate = false_phase();
  segment.base_body = load_phase(RegionSlotBank::State, 0);
  segment.preparations = {cut_phase(false), cut_phase(true)};
  segment.combine = concat_three_phase();
  return segment;
}

Value sequence(ValueTag tag, std::size_t length) {
  if (tag == ValueTag::String) {
    const char data[] = {'\0', 'A', static_cast<char>(0xff), 'z'};
    return payload::make_string_value(std::string(data, length));
  }
  if (tag == ValueTag::IntList) {
    const std::vector<Value> all{Value::from_int(-1), Value::from_int(0),
                                 Value::from_int(2), Value::from_int(9)};
    return payload::make_int_list_value(
        std::vector<Value>(all.begin(), all.begin() + length));
  }
  if (tag == ValueTag::FloatList) {
    const std::vector<Value> all{Value::from_float(-0.0), Value::from_float(1.5),
                                 Value::from_float(2.25), Value::from_float(-4.0)};
    return payload::make_float_list_value(
        std::vector<Value>(all.begin(), all.begin() + length));
  }
  const std::vector<Value> all{
      payload::make_string_value(""), payload::make_string_value("a"),
      payload::make_string_value(std::string(1, static_cast<char>(0xff))),
      payload::make_string_value("tail")};
  return payload::make_string_list_value(
      std::vector<Value>(all.begin(), all.begin() + length));
}

bool test_sequence_window_execution_and_fuel() {
  for (const ValueTag tag : {ValueTag::String, ValueTag::IntList,
                             ValueTag::FloatList, ValueTag::StringList}) {
    for (const std::size_t length : {std::size_t{0}, std::size_t{1},
                                     std::size_t{2}, std::size_t{4}}) {
      const Value source = sequence(tag, length);
      const BytecodeProgram program = invocation(sequence_segment(tag), {source});
      if (!valid(program, "sequence-window program")) return false;
      const ExecResult result = execute_bytecode_cpu(program, {}, 100);
      if (!check(!result.is_error && result.value.tag == tag &&
                     exact_value(result.value, source),
                 "sequence-window region failed exact reconstruction")) {
        return false;
      }
    }
  }

  const Value source = sequence(ValueTag::IntList, 4);
  const BytecodeProgram exact = invocation(sequence_segment(ValueTag::IntList), {source});
  for (int fuel = 0; fuel <= 7; ++fuel) {
    if (!check(is_error(execute_bytecode_cpu(exact, {}, fuel), ErrCode::Timeout),
               "seven-entry sequence tree ran without opcode plus entry fuel")) {
      return false;
    }
  }
  const ExecResult result = execute_bytecode_cpu(exact, {}, 8);
  return check(!result.is_error && exact_value(result.value, source),
               "seven-entry sequence tree failed at exact fuel eight");
}

bool test_opcode_bound_and_frame_error_precedence() {
  const BytecodeProgram ordinary = invocation(
      typed_unary_segment(Value::from_int(1), false), {Value::from_int(0)});
  if (!check(is_error(execute_bytecode_cpu(ordinary, {}, -1), ErrCode::Timeout),
             "negative fuel did not time out before bounded invocation")) {
    return false;
  }

  BytecodeProgram malformed = invocation(
      typed_unary_segment(Value::from_int(1), false), {Value::from_int(1)});
  malformed.bounded_region_segments[0].plan.version = 999;
  if (!check(is_error(execute_bytecode_cpu(malformed, {}, 0), ErrCode::Timeout),
             "opcode fuel did not precede malformed metadata") ||
      !check(is_error(execute_bytecode_cpu(malformed, {}, 1), ErrCode::Value),
             "paid malformed invocation did not report Value")) {
    return false;
  }

  BytecodeProgram missing = invocation(
      typed_unary_segment(Value::from_int(1), false), {});
  if (!check(is_error(execute_bytecode_cpu(missing, {}, 0), ErrCode::Timeout),
             "opcode fuel did not precede operand stack underflow") ||
      !check(is_error(execute_bytecode_cpu(missing, {}, 1), ErrCode::Value),
             "paid invocation did not report operand stack underflow")) {
    return false;
  }

  BytecodeProgram wrong_state = invocation(
      typed_unary_segment(Value::from_int(1), false), {Value::from_bool(true)});
  return check(is_error(execute_bytecode_cpu(wrong_state, {}, 1), ErrCode::Timeout),
               "state tag was checked before frame entry fuel") &&
         check(is_error(execute_bytecode_cpu(wrong_state, {}, 2), ErrCode::Type),
               "paid frame entry did not report exact state Type");
}

BoundedRegionSegment dynamic_1d_segment() {
  BoundedRegionSegment segment = typed_unary_segment(Value::from_int(1), false);
  segment.plan.bound_operand_count = 2;
  segment.plan.coordinate_domains = {
      {operand_bound(1), operand_bound(2)}};
  return segment;
}

bool test_dynamic_bounds_before_frames_and_overflow() {
  const auto run = [](std::vector<Value> values, int fuel) {
    return execute_bytecode_cpu(invocation(dynamic_1d_segment(), std::move(values)),
                                {}, fuel);
  };
  if (!check(is_error(run({Value::from_int(0), Value::from_int(0),
                            Value::from_bool(true)}, 1), ErrCode::Type),
             "bound operand tag was checked after frame entry") ||
      !check(is_error(run({Value::from_int(0), Value::from_int(1),
                            Value::from_int(-1)}, 1), ErrCode::Value),
             "negative/inverted dynamic domain reached a frame") ||
      !check(is_error(run({Value::from_int(0),
                            Value::from_int(std::numeric_limits<std::int64_t>::min()),
                            Value::from_int(std::numeric_limits<std::int64_t>::max())}, 1),
                      ErrCode::Value),
             "full-width dynamic domain failed to reject exact offset overflow")) {
    return false;
  }

  constexpr std::int64_t kLargeOffset = INT64_C(-9007199254740993);
  BoundedRegionSegment exact = typed_unary_segment(Value::from_int(0), false);
  exact.plan.requests[0].states[0].offset = kLargeOffset;
  exact.plan.memoized = false;
  exact.plan.limits.cells = 0;
  exact.base_predicate = false_phase();
  exact.boundary = load_phase(RegionSlotBank::State, 0);
  const BytecodeProgram exact_program = invocation(std::move(exact), {Value::from_int(1)});
  if (!valid(exact_program, "exact Int64 offset program")) return false;
  const ExecResult exact_result = execute_bytecode_cpu(exact_program, {}, 10);
  if (!check(!exact_result.is_error && exact_result.value.tag == ValueTag::Int &&
                 exact_result.value.i == 1 + kLargeOffset,
             "coordinate offset was narrowed through floating point")) {
    return false;
  }
  return true;
}

BoundedRegionSegment request_error_segment() {
  BoundedRegionSegment segment;
  RegionPlan& plan = segment.plan;
  plan.state_types = {ValueTag::Int, ValueTag::Int};
  plan.result_type = ValueTag::Int;
  plan.request_expression_types = {ValueTag::Int, ValueTag::Int};
  RegionStateTransition first_expression;
  first_expression.kind = RegionTransitionKind::Expression;
  first_expression.expression = 0;
  RegionStateTransition second_expression = first_expression;
  second_expression.expression = 1;
  plan.requests = {{{offset(0, -1), first_expression}},
                   {{offset(0, -1), second_expression}}};
  plan.duplicate_policy = DuplicatePolicy::Allow;
  plan.limits = {1, 0, 1};
  plan.progress = RegionProgressKind::Coordinates;
  plan.coordinate_slots = {0};
  plan.coordinate_rank = {{0, 1}};
  plan.coordinate_domains = {{literal_bound(0), literal_bound(2)}};
  plan.coordinate_endpoint = DomainEndpoint::Inclusive;
  segment.boundary = constant_phase(Value::from_int(0));
  segment.base_predicate = coordinate_base_predicate_1d();
  segment.base_body = constant_phase(Value::from_int(0));
  segment.request_expressions = {
      zero_div_phase(ValueTag::Int), constant_phase(Value::from_int(8))};
  segment.combine = load_phase(RegionSlotBank::Result, 0);
  return segment;
}

bool test_capacity_and_phase_error_ordering() {
  BoundedRegionSegment frame_limited = typed_unary_segment(Value::from_int(1), false);
  frame_limited.plan.limits.frames = 1;
  BytecodeProgram frame_program = invocation(std::move(frame_limited), {Value::from_int(1)});
  if (!check(is_error(execute_bytecode_cpu(frame_program, {}, 2), ErrCode::Timeout),
             "rejected child consumed frame entry fuel or bypassed capacity")) {
    return false;
  }

  const BytecodeProgram request_error = invocation(
      request_error_segment(), {Value::from_int(1), Value::from_int(0)});
  if (!valid(request_error, "request-error program")) return false;
  if (!check(is_error(execute_bytecode_cpu(request_error, {}, 2), ErrCode::ZeroDiv),
             "first request construction error did not precede frame capacity")) {
    return false;
  }

  BoundedRegionSegment no_root = typed_unary_segment(Value::from_int(1), false);
  no_root.plan.limits.frames = 0;
  BytecodeProgram no_root_program = invocation(std::move(no_root), {Value::from_int(0)});
  if (!check(is_error(execute_bytecode_cpu(no_root_program, {}, 1), ErrCode::Timeout),
             "zero frame capacity entered the root")) {
    return false;
  }

  BoundedRegionSegment no_cells = typed_unary_segment(Value::from_int(1), false);
  no_cells.plan.limits.cells = 0;
  BytecodeProgram no_cells_program = invocation(std::move(no_cells), {Value::from_int(1)});
  if (!check(is_error(execute_bytecode_cpu(no_cells_program, {}, 10), ErrCode::Timeout),
             "zero memo-cell capacity accepted a successful combine")) {
    return false;
  }

  BoundedRegionSegment combine_before_cells =
      typed_unary_segment(Value::from_int(1), false);
  combine_before_cells.plan.limits.cells = 0;
  combine_before_cells.combine = zero_div_phase(ValueTag::Int);
  BytecodeProgram combine_error_program =
      invocation(std::move(combine_before_cells), {Value::from_int(1)});
  if (!valid(combine_error_program, "combine-before-cells program")) return false;
  if (!check(is_error(execute_bytecode_cpu(combine_error_program, {}, 10),
                      ErrCode::ZeroDiv),
             "memo capacity error incorrectly preceded combine error")) {
    return false;
  }

  BoundedRegionSegment boundary_error = typed_unary_segment(Value::from_int(1), false);
  boundary_error.boundary = zero_div_phase(ValueTag::Int);
  BytecodeProgram outside = invocation(std::move(boundary_error), {Value::from_int(-1)});
  if (!valid(outside, "boundary-error program")) return false;
  if (!check(is_error(execute_bytecode_cpu(outside, {}, 2), ErrCode::ZeroDiv),
             "boundary error did not precede base and recursion")) {
    return false;
  }

  BoundedRegionSegment base_error = typed_unary_segment(Value::from_int(1), false);
  base_error.base_predicate = zero_div_phase(ValueTag::Bool);
  BytecodeProgram inside = invocation(std::move(base_error), {Value::from_int(0)});
  if (!valid(inside, "base-error program")) return false;
  return check(is_error(execute_bytecode_cpu(inside, {}, 2), ErrCode::ZeroDiv),
               "base predicate error did not precede memo and preparation");
}

bool test_lazy_captured_local_and_runtime_tag_guard() {
  BoundedRegionSegment unused = typed_unary_segment(Value::from_int(5), false);
  unused.plan.parameter_types = {ValueTag::Int};
  unused.parameter_locals = {0};
  BytecodeProgram lazy = invocation(std::move(unused), {Value::from_int(0)}, 1);
  if (!valid(lazy, "lazy capture program")) return false;
  const ExecResult success = execute_bytecode_cpu(lazy, {}, 10);
  if (!check(!success.is_error && success.value.tag == ValueTag::Int &&
                 success.value.i == 5,
             "unused unset caller capture was read eagerly")) {
    return false;
  }
  const ExecResult unused_wrong =
      execute_bytecode_cpu(lazy, {{0, Value::from_bool(true)}}, 10);
  if (!check(!unused_wrong.is_error && unused_wrong.value.tag == ValueTag::Int &&
                 unused_wrong.value.i == 5,
             "unused runtime-wrong capture was type-checked eagerly")) {
    return false;
  }

  BoundedRegionSegment read = typed_unary_segment(Value::from_int(5), false);
  read.plan.parameter_types = {ValueTag::Int};
  read.parameter_locals = {0};
  read.base_predicate = phase(
      {Value::from_int(0)},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::PushConst, 0),
       ins(Opcode::Eq), ins(Opcode::Return)},
      {{slot(RegionSlotBank::Parameter, 0), 0}}, 1);
  BytecodeProgram reader = invocation(std::move(read), {Value::from_int(0)}, 1);
  if (!valid(reader, "capture reader")) return false;
  if (!check(is_error(execute_bytecode_cpu(reader, {}, 10), ErrCode::Name),
             "read of unset captured local did not report Name")) {
    return false;
  }
  if (!check(is_error(execute_bytecode_cpu(
                          reader, {{0, Value::from_bool(true)}}, 10),
                      ErrCode::Type),
             "declared Int capture bypassed the lazy LOAD Type guard")) {
    return false;
  }

  BoundedRegionSegment overwritten =
      typed_unary_segment(Value::from_int(5), false);
  overwritten.plan.parameter_types = {ValueTag::Int};
  overwritten.parameter_locals = {0};
  overwritten.base_predicate = phase(
      {Value::from_bool(true)},
      {ins_a(Opcode::PushConst, 0), ins_a(Opcode::Store, 0),
       ins_a(Opcode::Load, 0), ins(Opcode::Return)},
      {{slot(RegionSlotBank::Parameter, 0), 0}}, 1);
  BytecodeProgram store_first =
      invocation(std::move(overwritten), {Value::from_int(0)}, 1);
  if (!valid(store_first, "capture overwrite program")) return false;
  const ExecResult overwritten_result = execute_bytecode_cpu(
      store_first, {{0, Value::from_bool(false)}}, 10);
  if (!check(!overwritten_result.is_error &&
                 overwritten_result.value.tag == ValueTag::Int &&
                 overwritten_result.value.i == 5,
             "STORE did not replace the captured local's pending tag check")) {
    return false;
  }

  BoundedRegionSegment charged =
      typed_unary_segment(Value::from_int(5), false);
  charged.plan.parameter_types = {ValueTag::Int};
  charged.parameter_locals = {0};
  charged.base_predicate = phase(
      {Value::from_int(0)},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::PushConst, 0),
       ins(Opcode::Eq), ins(Opcode::Return)},
      {{slot(RegionSlotBank::Parameter, 0), 0}}, 1);
  charged.base_predicate.program.instruction_fuel = {3, 0, 0, 0};
  BytecodeProgram charged_load =
      invocation(std::move(charged), {Value::from_int(0)}, 1);
  if (!valid(charged_load, "charged capture LOAD program")) return false;
  if (!check(is_error(execute_bytecode_cpu(
                          charged_load, {{0, Value::from_int(0)}}, 4),
                      ErrCode::Timeout),
             "charged capture LOAD ran below its exact fuel boundary")) {
    return false;
  }
  const ExecResult exact = execute_bytecode_cpu(
      charged_load, {{0, Value::from_int(0)}}, 5);
  return check(!exact.is_error && exact.value.tag == ValueTag::Int &&
                   exact.value.i == 5,
               "charged capture LOAD failed at its exact fuel boundary");
}

bool test_malformed_phase_metadata_rejected() {
  BytecodeProgram malformed = invocation(
      typed_unary_segment(Value::from_int(1), false), {Value::from_int(0)});
  malformed.bounded_region_segments[0].base_body.program.var2idx["forged"] = 0;
  const BytecodeVerifyResult verified = verify_bytecode(malformed);
  if (!check(!verified.ok &&
                 verified.diagnostic.code == BytecodeVerifyCode::InvalidVarMapping,
             "malformed generic phase metadata passed verification")) {
    return false;
  }
  return check(is_error(execute_bytecode_cpu(malformed, {}, 1), ErrCode::Value),
               "malformed phase metadata reached bounded execution");
}

}  // namespace

int main() {
  payload::clear();
  if (!test_custom_coordinate_execution()) return 1;
  if (!test_all_exact_result_tags_and_duplicate_memo()) return 1;
  if (!test_sequence_window_execution_and_fuel()) return 1;
  if (!test_opcode_bound_and_frame_error_precedence()) return 1;
  if (!test_dynamic_bounds_before_frames_and_overflow()) return 1;
  if (!test_capacity_and_phase_error_ordering()) return 1;
  if (!test_lazy_captured_local_and_runtime_tag_guard()) return 1;
  if (!test_malformed_phase_metadata_rejected()) return 1;
  std::cout << "gagp_test_region_execution: OK\n";
  return 0;
}
