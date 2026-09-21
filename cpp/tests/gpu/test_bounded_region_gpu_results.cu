#define main gagp_capture_tool_main
#include "../../src/bench/capture_gpu_results.cu"
#undef main

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "gagp/cli/region_codec.hpp"
#include "gagp/core/builtin.hpp"
#include "gagp/core/bytecode.hpp"
#include "gagp/core/bytecode_verify.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace {

using namespace gagp;
using gagp::cli_detail::JsonParser;
using gagp::cli_detail::JsonValue;
using gagp::cli_detail::require_int;
using gagp::cli_detail::require_object_field;
using gagp::cli_detail::require_string;

Instr ins(Opcode op) { return {op, 0, 0, false, false}; }
Instr ins_a(Opcode op, int a) { return {op, a, 0, true, false}; }
Instr ins_ab(Opcode op, int a, int b) { return {op, a, b, true, true}; }

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << '\n';
  return condition;
}

RegionValueSlot slot(RegionSlotBank bank, std::uint32_t index) {
  return {bank, index};
}

RegionPhase phase(std::vector<Value> constants, std::vector<Instr> code,
                  std::vector<RegionPhaseBinding> bindings = {},
                  int n_locals = 0) {
  RegionPhase result;
  result.program.consts = std::move(constants);
  result.program.code = std::move(code);
  result.program.n_locals = n_locals;
  result.program.instruction_fuel.assign(result.program.code.size(), 0);
  result.bindings = std::move(bindings);
  return result;
}

RegionPhase constant_phase(Value value) {
  return phase({value}, {ins_a(Opcode::PushConst, 0), ins(Opcode::Return)});
}

RegionPhase load_phase(RegionSlotBank bank, std::uint32_t index) {
  return phase({}, {ins_a(Opcode::Load, 0), ins(Opcode::Return)},
               {{slot(bank, index), 0}}, 1);
}

RegionPhase zero_base_predicate() {
  return phase(
      {Value::from_int(0)},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::PushConst, 0), ins(Opcode::Eq),
       ins(Opcode::Return)},
      {{slot(RegionSlotBank::State, 0), 0}}, 1);
}

Value nominal_value(ValueTag tag) {
  switch (tag) {
    case ValueTag::Int: return Value::from_int(0);
    case ValueTag::Float: return Value::from_float(0.0);
    case ValueTag::Bool: return Value::from_bool(false);
    case ValueTag::Char: return Value::from_char(0);
    case ValueTag::String: return payload::make_string_value("");
    case ValueTag::IntList: return payload::make_int_list_value({});
    case ValueTag::FloatList: return payload::make_float_list_value({});
    case ValueTag::StringList: return payload::make_string_list_value({});
    case ValueTag::FallbackToken:
    case ValueTag::Invalid: return Value::invalid();
  }
  return Value::invalid();
}

RegionPhase concat_phase(Value left, Value right) {
  return phase(
      {left, right},
      {ins_a(Opcode::PushConst, 0), ins_a(Opcode::PushConst, 1),
       ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Concat), 2),
       ins(Opcode::Return)});
}

RegionPhase concat_index_phase(Value left, Value right) {
  return phase(
      {left, right, Value::from_int(0)},
      {ins_a(Opcode::PushConst, 0), ins_a(Opcode::PushConst, 1),
       ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Concat), 2),
       ins_a(Opcode::PushConst, 2),
       ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Index), 2),
       ins(Opcode::Return)});
}

RegionStateTransition offset(std::int64_t amount) {
  RegionStateTransition result;
  result.kind = RegionTransitionKind::CoordinateOffset;
  result.source_state = 0;
  result.offset = amount;
  return result;
}

RegionStateTransition copy(std::uint32_t source) {
  RegionStateTransition result;
  result.kind = RegionTransitionKind::CopyState;
  result.source_state = source;
  return result;
}

RegionBound literal(std::int64_t value) {
  RegionBound result;
  result.kind = RegionBoundKind::Literal;
  result.literal = value;
  return result;
}

BoundedRegionSegment unary_segment(ValueTag result_type,
                                   RegionPhase base_body,
                                   RegionPhase combine,
                                   std::vector<std::int64_t> request_offsets = {-1}) {
  BoundedRegionSegment segment;
  RegionPlan& plan = segment.plan;
  plan.state_types = {ValueTag::Int};
  plan.result_type = result_type;
  for (const std::int64_t amount : request_offsets) {
    plan.requests.push_back({{offset(amount)}});
  }
  plan.limits = {128, 128, 1};
  plan.memoized = true;
  plan.duplicate_policy = DuplicatePolicy::Reject;
  plan.coordinate_slots = {0};
  plan.coordinate_rank = {{0, 1}};
  plan.coordinate_domains = {{{literal(0), literal(8)}}};
  plan.coordinate_endpoint = DomainEndpoint::Inclusive;
  segment.boundary = constant_phase(nominal_value(result_type));
  segment.base_predicate = zero_base_predicate();
  segment.base_body = std::move(base_body);
  segment.combine = std::move(combine);
  return segment;
}

BytecodeProgram invocation(BoundedRegionSegment segment, std::int64_t state) {
  BytecodeProgram program;
  program.consts = {Value::from_int(state)};
  program.code = {ins_a(Opcode::PushConst, 0),
                  ins_a(Opcode::BoundedRegion, 0), ins(Opcode::Return)};
  program.instruction_fuel = {0, 1, 0};
  program.bounded_region_segments.push_back(std::move(segment));
  return program;
}

BytecodeProgram ordinary(std::vector<Value> constants,
                         std::vector<Instr> code) {
  BytecodeProgram program;
  program.consts = std::move(constants);
  program.code = std::move(code);
  program.instruction_fuel.assign(program.code.size(), 0);
  return program;
}

struct Observed {
  bool is_error = false;
  std::string error;
  int tag = -1;
  std::string bits;
  std::string bytes_hex;
  int fuel_left = 0;
};

Observed probe(const BytecodeProgram& program, int fuel) {
  bool timeout = false;
  const JsonValue row = JsonParser(
      gagp::migration::probe(program, {}, fuel, &timeout)).parse();
  Observed out;
  const JsonValue& error = require_object_field(row, "error");
  out.is_error = error.kind != JsonValue::Kind::Null;
  if (out.is_error) {
    out.error = require_string(error, "error");
  } else {
    const JsonValue& value = require_object_field(row, "value");
    out.tag = require_int(require_object_field(value, "tag"), "tag");
    out.bits = require_string(require_object_field(value, "bits"), "bits");
    const auto bytes = value.object_v.find("bytes_hex");
    if (bytes != value.object_v.end()) {
      out.bytes_hex = require_string(bytes->second, "bytes_hex");
    }
  }
  out.fuel_left = require_int(require_object_field(row, "fuel_left"),
                              "fuel_left");
  return out;
}

bool valid(const BytecodeProgram& program, const std::string& label) {
  const BytecodeVerifyResult result = verify_bytecode(program);
  return check(result.ok, label + ": invalid fixture: " +
                              result.diagnostic.message);
}

bool same_value(const Observed& left, const Observed& right) {
  return !left.is_error && !right.is_error && left.tag == right.tag &&
         left.bits == right.bits;
}

std::string encoded_bits(const Value& value) {
  const JsonValue encoded =
      JsonParser(migration::encode_value(value, false)).parse();
  return require_string(require_object_field(encoded, "bits"), "bits");
}

std::string encoded_bytes(const Value& value) {
  const JsonValue encoded =
      JsonParser(migration::encode_value(value, false)).parse();
  return require_string(require_object_field(encoded, "bytes_hex"),
                        "bytes_hex");
}

BytecodeProgram concat_control(Value left, Value right) {
  return ordinary(
      {left, right},
      {ins_a(Opcode::PushConst, 0), ins_a(Opcode::PushConst, 1),
       ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Concat), 2),
       ins(Opcode::Return)});
}

BytecodeProgram index_control(Value left, Value right) {
  return ordinary(
      {left, right, Value::from_int(0)},
      {ins_a(Opcode::PushConst, 0), ins_a(Opcode::PushConst, 1),
       ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Concat), 2),
       ins_a(Opcode::PushConst, 2),
       ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Index), 2),
       ins(Opcode::Return)});
}

std::pair<Value, Value> list_halves(ValueTag tag) {
  if (tag == ValueTag::IntList) {
    std::vector<Value> left(64, Value::from_int(7));
    std::vector<Value> right(65, Value::from_int(9));
    return {payload::make_int_list_value(left),
            payload::make_int_list_value(right)};
  }
  if (tag == ValueTag::FloatList) {
    std::vector<Value> left(64, Value::from_float(1.25));
    std::vector<Value> right(65, Value::from_float(-2.5));
    return {payload::make_float_list_value(left),
            payload::make_float_list_value(right)};
  }
  const Value item = payload::make_string_value("s");
  std::vector<Value> left(64, item);
  std::vector<Value> right(65, item);
  return {payload::make_string_list_value(left),
          payload::make_string_list_value(right)};
}

bool test_public_capture_decoder() {
  JsonValue raw = JsonParser(R"({"n_locals":0,"consts":[{"type":"int","value":0}],
    "code":[{"op":"PUSH_CONST","a":0},{"op":"BOUNDED_REGION","a":0},{"op":"RETURN"}],
    "instruction_fuel":[0,1,0],"segments":{"bounded_region":[]}})").parse();
  auto segment = unary_segment(ValueTag::Int, constant_phase(Value::from_int(7)),
                               load_phase(RegionSlotBank::Result, 0));
  raw.object_v.at("segments").object_v.at("bounded_region").array_v.push_back(
      cli_detail::encode_bounded_region_segment(segment));
  const auto decoded = migration::decode_probe_bytecode(raw);
  const auto actual = probe(decoded, 2);
  if (!check(!actual.is_error && actual.tag == static_cast<int>(ValueTag::Int) &&
                 actual.bits == encoded_bits(Value::from_int(7)) && actual.fuel_left == 0,
             "public capture codec lost the bounded descriptor or fuel")) return false;
  JsonValue version;
  version.kind = JsonValue::Kind::String;
  version.string_v = "migration-bytecode-v99";
  raw.object_v["format_version"] = version;
  try {
    (void)migration::decode_probe_bytecode(raw);
  } catch (const std::exception&) {
    return true;
  }
  return check(false, "capture decoder accepted an unknown migration version");
}

bool test_exact_string_and_fuel() {
  const Value exact = payload::make_string_value(std::string(512, 'x'));
  const BytecodeProgram program = invocation(
      unary_segment(ValueTag::String,
                    concat_phase(payload::make_string_value(std::string(256, 'x')),
                                 payload::make_string_value(std::string(256, 'x'))),
                    load_phase(RegionSlotBank::Result, 0)),
      0);
  if (!valid(program, "512-byte exact String")) return false;
  const ExecResult cpu = execute_bytecode_cpu(program, {}, 2);
  const Observed at_boundary = probe(program, 2);
  const Observed below = probe(program, 1);
  return check(!cpu.is_error && cpu.value.tag == ValueTag::String &&
                   cpu.value.i == exact.i,
               "CPU exact String control changed") &&
         check(!at_boundary.is_error &&
                   at_boundary.tag == static_cast<int>(ValueTag::String) &&
                   at_boundary.bits == encoded_bits(exact) &&
                   at_boundary.bytes_hex == encoded_bytes(exact),
               "GPU did not preserve the exact 512-byte String") &&
         check(at_boundary.fuel_left == 0,
               "exact bounded result consumed the wrong fuel") &&
         check(below.is_error && below.error == "Timeout",
               "bounded result succeeded below its exact fuel boundary");
}

bool test_string_fallback() {
  const Value left = payload::make_string_value(std::string(256, 'a'));
  const Value right = payload::make_string_value(std::string(257, 'b'));
  const BytecodeProgram bounded = invocation(
      unary_segment(ValueTag::String, concat_phase(left, right),
                    load_phase(RegionSlotBank::Result, 0)),
      0);
  const BytecodeProgram control = concat_control(left, right);
  if (!valid(bounded, "513-byte String fallback") ||
      !valid(control, "ordinary String fallback control")) {
    return false;
  }
  const Observed actual = probe(bounded, 2);
  const Observed expected = probe(control, 0);
  return check(same_value(actual, expected) &&
                   actual.tag == static_cast<int>(ValueTag::FallbackToken),
               "513-byte bounded concat changed its fallback token");
}

bool test_compact_lists_and_index() {
  for (const ValueTag tag : {ValueTag::IntList, ValueTag::FloatList,
                             ValueTag::StringList}) {
    const auto halves = list_halves(tag);
    const BytecodeProgram compact = invocation(
        unary_segment(tag, concat_phase(halves.first, halves.second),
                      load_phase(RegionSlotBank::Result, 0)),
        0);
    const BytecodeProgram compact_control =
        concat_control(halves.first, halves.second);
    const BytecodeProgram indexed = invocation(
        unary_segment(tag == ValueTag::IntList
                          ? ValueTag::Int
                          : tag == ValueTag::FloatList ? ValueTag::Float
                                                      : ValueTag::String,
                      concat_index_phase(halves.first, halves.second),
                      load_phase(RegionSlotBank::Result, 0)),
        0);
    const BytecodeProgram indexed_control =
        index_control(halves.first, halves.second);
    if (!valid(compact, "compact list") || !valid(indexed, "compact index")) {
      return false;
    }
    const Observed compact_actual = probe(compact, 2);
    const Observed compact_expected = probe(compact_control, 0);
    const Observed index_actual = probe(indexed, 2);
    const Observed index_expected = probe(indexed_control, 0);
    if (!check(same_value(compact_actual, compact_expected) &&
                   compact_actual.tag == static_cast<int>(tag),
               "129-element compact list lost its typed-list tag") ||
        !check(same_value(index_actual, index_expected) &&
                   index_actual.tag ==
                       static_cast<int>(ValueTag::FallbackToken),
               "indexing a compact list changed its fallback token")) {
      return false;
    }
  }
  return true;
}

bool test_exact_payload_memo_roots() {
  const RegionPhase combine = phase(
      {}, {ins_a(Opcode::Load, 0), ins_a(Opcode::Load, 1),
           ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Concat), 2),
           ins(Opcode::Return)},
      {{slot(RegionSlotBank::Result, 0), 0},
       {slot(RegionSlotBank::Result, 1), 1}}, 2);
  for (const ValueTag tag : {ValueTag::String, ValueTag::IntList,
                             ValueTag::FloatList, ValueTag::StringList}) {
    RegionPhase base;
    Value expected = Value::invalid();
    Value indexed_expected = Value::invalid();
    if (tag == ValueTag::String) {
      base = constant_phase(payload::make_string_value("x"));
      expected = payload::make_string_value("xxxx");
      indexed_expected = Value::from_char('x');
    } else if (tag == ValueTag::IntList) {
      const Value item = Value::from_int(7);
      base = constant_phase(payload::make_int_list_value({item}));
      expected = payload::make_int_list_value(std::vector<Value>(4, item));
      indexed_expected = item;
    } else if (tag == ValueTag::FloatList) {
      const Value item = Value::from_float(1.25);
      base = constant_phase(payload::make_float_list_value({item}));
      expected = payload::make_float_list_value(std::vector<Value>(4, item));
      indexed_expected = item;
    } else {
      // The memo-held list must retain dynamically generated child strings too.
      base = phase(
          {payload::make_string_list_value({}), payload::make_string_value("u"),
           payload::make_string_value("v")},
          {ins_a(Opcode::PushConst, 0), ins_a(Opcode::PushConst, 1),
           ins_a(Opcode::PushConst, 2),
           ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Concat), 2),
           ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Append), 2),
           ins(Opcode::Return)});
      expected = payload::make_string_list_value(
          std::vector<Value>(4, payload::make_string_value("uv")));
      indexed_expected = Value::from_char('v');
    }
    auto segment = unary_segment(tag, std::move(base), combine, {-1, -1});
    segment.plan.duplicate_policy = DuplicatePolicy::Allow;
    const BytecodeProgram program = invocation(std::move(segment), 2);
    BytecodeProgram indexed = program;
    indexed.code.pop_back();
    indexed.consts.push_back(Value::from_int(3));
    indexed.code.push_back(ins_a(Opcode::PushConst, 1));
    indexed.code.push_back(ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Index), 2));
    if (tag == ValueTag::StringList) {
      // Reading a character requires the child's bytes, unlike len(), which can
      // be answered from the packed token without retaining its payload.
      indexed.consts.push_back(Value::from_int(1));
      indexed.code.push_back(ins_a(Opcode::PushConst, 2));
      indexed.code.push_back(ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Index), 2));
    }
    indexed.code.push_back(ins(Opcode::Return));
    indexed.instruction_fuel.resize(indexed.code.size(), 0);
    const std::string label = "exact memo payload tag " + std::to_string(static_cast<int>(tag));
    if (!valid(program, label) || !valid(indexed, label + " dereference")) return false;
    const ExecResult cpu = execute_bytecode_cpu(program, {}, 6);
    const ExecResult cpu_indexed = execute_bytecode_cpu(indexed, {}, 6);
    const ExecResult cpu_below = execute_bytecode_cpu(program, {}, 5);
    const Observed actual = probe(program, 6);
    const Observed dereferenced = probe(indexed, 6);
    const Observed below = probe(program, 5);
    // Five region entries plus the root operation cost six. Without the state-1
    // memo hit, the duplicate request needs two additional terminal entries.
    // Dereference on device: host registry contents must not mask a lost payload
    // when the raw result is serialized after execution.
    if (!check(!cpu.is_error && cpu.value.tag == tag && cpu.value.i == expected.i &&
                   !cpu_indexed.is_error && cpu_indexed.value.tag == indexed_expected.tag &&
                   encoded_bits(cpu_indexed.value) == encoded_bits(indexed_expected),
               label + ": CPU fixture changed") ||
        !check(cpu_below.is_error && cpu_below.err.code == ErrCode::Timeout,
               label + ": CPU fuel boundary changed") ||
        !check(!actual.is_error && actual.tag == static_cast<int>(tag) &&
                   actual.bits == encoded_bits(expected) && actual.fuel_left == 0 &&
                   (tag != ValueTag::String || actual.bytes_hex == encoded_bytes(expected)),
               label + ": memo lost the exact payload result") ||
        !check(!dereferenced.is_error &&
                   dereferenced.tag == static_cast<int>(indexed_expected.tag) &&
                   dereferenced.bits == encoded_bits(indexed_expected) && dereferenced.fuel_left == 0,
               label + ": memo-held payload was not readable on device") ||
        !check(below.is_error && below.error == "Timeout",
               label + ": memo hit fuel boundary changed")) return false;
  }
  return true;
}

bool test_memoized_identity_fallback() {
  const Value left = payload::make_string_value(std::string(256, 'm'));
  const Value right = payload::make_string_value(std::string(257, 'n'));
  auto segment = unary_segment(ValueTag::String, concat_phase(left, right),
                               load_phase(RegionSlotBank::Result, 0), {-1, -1});
  segment.plan.duplicate_policy = DuplicatePolicy::Allow;
  const BytecodeProgram bounded = invocation(std::move(segment), 2);
  if (!valid(bounded, "fallback memo hit")) return false;
  // Root, state 1, two terminal state-0 entries, then a memo hit at state 1.
  // A missed memo lookup would require two additional frame entry charges.
  const Observed actual = probe(bounded, 6);
  const Observed expected = probe(concat_control(left, right), 0);
  const Observed below = probe(bounded, 5);
  return check(same_value(actual, expected) &&
                   actual.tag == static_cast<int>(ValueTag::FallbackToken),
               "memoized identity combine did not preserve fallback") &&
         check(actual.fuel_left == 0,
               "memoized identity recurrence consumed the wrong fuel") &&
         check(below.is_error && below.error == "Timeout",
               "memoized identity recurrence succeeded below its fuel boundary");
}

bool test_physical_frame_capacity() {
  BoundedRegionSegment segment = unary_segment(
      ValueTag::Int, constant_phase(Value::from_int(7)),
      load_phase(RegionSlotBank::Result, 0));
  segment.plan.memoized = false;
  segment.plan.limits.cells = 0;
  segment.plan.coordinate_domains = {{{literal(0), literal(200)}}};
  const BytecodeProgram exact = invocation(segment, 127);
  const BytecodeProgram overflow = invocation(std::move(segment), 128);
  if (!valid(exact, "128 physical frames") ||
      !valid(overflow, "129th physical frame")) {
    return false;
  }

  const ExecResult cpu_exact = execute_bytecode_cpu(exact, {}, 1000);
  const ExecResult cpu_overflow = execute_bytecode_cpu(overflow, {}, 1000);
  const Observed gpu_exact = probe(exact, 1000);
  const Observed gpu_overflow = probe(overflow, 1000);
  return check(!cpu_exact.is_error && cpu_exact.value.tag == ValueTag::Int &&
                   cpu_exact.value.i == 7,
               "CPU control did not complete at 128 physical frames") &&
         check(cpu_overflow.is_error &&
                   cpu_overflow.err.code == ErrCode::Timeout,
               "CPU control did not time out before the 129th frame") &&
         check(!gpu_exact.is_error &&
                   gpu_exact.tag == static_cast<int>(ValueTag::Int) &&
                   gpu_exact.bits == encoded_bits(Value::from_int(7)),
               "GPU did not complete at 128 physical frames") &&
         check(gpu_overflow.is_error && gpu_overflow.error == "Timeout",
               "GPU did not time out before the 129th physical frame");
}

bool test_physical_memo_capacity() {
  BoundedRegionSegment segment = unary_segment(
      ValueTag::Int, constant_phase(Value::from_int(7)),
      load_phase(RegionSlotBank::Result, 0), {-2, -1});
  segment.plan.coordinate_domains = {{{literal(0), literal(200)}}};
  const BytecodeProgram exact = invocation(segment, 128);
  const BytecodeProgram overflow = invocation(std::move(segment), 129);
  if (!valid(exact, "128 physical memo cells") ||
      !valid(overflow, "129th physical memo insertion")) {
    return false;
  }

  const ExecResult cpu_exact = execute_bytecode_cpu(exact, {}, 1000);
  const ExecResult cpu_overflow = execute_bytecode_cpu(overflow, {}, 1000);
  const Observed gpu_exact = probe(exact, 1000);
  const Observed gpu_overflow = probe(overflow, 1000);
  return check(!cpu_exact.is_error && cpu_exact.value.tag == ValueTag::Int &&
                   cpu_exact.value.i == 7,
               "CPU control did not complete after 128 memo insertions") &&
         check(cpu_overflow.is_error &&
                   cpu_overflow.err.code == ErrCode::Timeout,
               "CPU control did not time out on the 129th memo insertion") &&
         check(!gpu_exact.is_error &&
                   gpu_exact.tag == static_cast<int>(ValueTag::Int) &&
                   gpu_exact.bits == encoded_bits(Value::from_int(7)),
               "GPU did not complete after 128 memo insertions") &&
         check(gpu_overflow.is_error && gpu_overflow.error == "Timeout",
               "GPU did not time out on the 129th memo insertion");
}

RegionPhase increment_prepared_phase(std::uint32_t index) {
  return phase(
      {Value::from_int(1)},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::PushConst, 0),
       ins(Opcode::Add), ins(Opcode::Return)},
      {{slot(RegionSlotBank::Prepared, index), 0}}, 1);
}

bool test_maximum_state_request_preparation_shape() {
  BoundedRegionSegment segment;
  RegionPlan& plan = segment.plan;
  plan.state_types = {ValueTag::Int, ValueTag::Int, ValueTag::Int,
                      ValueTag::Int};
  plan.result_type = ValueTag::Int;
  plan.preparations = {
      {ValueTag::Int, RegionPreparationKind::Identity},
      {ValueTag::Int, RegionPreparationKind::Identity},
      {ValueTag::Int, RegionPreparationKind::Identity},
      {ValueTag::Int, RegionPreparationKind::Identity}};
  const RegionRequest request{{offset(-1), copy(1), copy(2), copy(3)}};
  plan.requests.assign(8, request);
  plan.limits = {16, 0, 1};
  plan.duplicate_policy = DuplicatePolicy::Allow;
  plan.coordinate_slots = {0};
  plan.coordinate_rank = {{0, 1}};
  plan.coordinate_domains = {{{literal(0), literal(200)}}};
  plan.coordinate_endpoint = DomainEndpoint::Inclusive;

  segment.boundary = constant_phase(Value::from_int(0));
  segment.base_predicate = zero_base_predicate();
  segment.base_body = load_phase(RegionSlotBank::State, 3);
  segment.preparations = {
      constant_phase(Value::from_int(10)), increment_prepared_phase(0),
      increment_prepared_phase(1), increment_prepared_phase(2)};
  segment.combine = phase(
      {},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::Load, 1), ins(Opcode::Add),
       ins_a(Opcode::Load, 2), ins(Opcode::Add), ins_a(Opcode::Load, 3),
       ins(Opcode::Add), ins(Opcode::Return)},
      {{slot(RegionSlotBank::Result, 0), 0},
       {slot(RegionSlotBank::Result, 7), 1},
       {slot(RegionSlotBank::Prepared, 3), 2},
       {slot(RegionSlotBank::State, 3), 3}},
      4);

  BytecodeProgram shaped;
  shaped.consts = {Value::from_int(1), Value::from_int(2),
                   Value::from_int(3), Value::from_int(4)};
  shaped.code = {ins_a(Opcode::PushConst, 0), ins_a(Opcode::PushConst, 1),
                 ins_a(Opcode::PushConst, 2), ins_a(Opcode::PushConst, 3),
                 ins_a(Opcode::BoundedRegion, 0), ins(Opcode::Return)};
  shaped.instruction_fuel = {0, 0, 0, 0, 1, 0};
  shaped.bounded_region_segments.push_back(std::move(segment));
  if (!valid(shaped, "4-state/8-request/4-preparation execution")) return false;

  const ExecResult cpu = execute_bytecode_cpu(shaped, {}, 100);
  const Observed gpu = probe(shaped, 100);
  return check(!cpu.is_error && cpu.value.tag == ValueTag::Int &&
                   cpu.value.i == 25,
               "CPU maximum-shape control changed") &&
         check(!gpu.is_error && gpu.tag == static_cast<int>(ValueTag::Int) &&
                   gpu.bits == encoded_bits(Value::from_int(25)),
               "GPU did not execute the highest state/request/preparation slots");
}

RegionPhase mixed_base(Value exact, Value left, Value right) {
  return phase(
      {Value::from_int(0), exact, left, right},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::PushConst, 0), ins(Opcode::Eq),
       ins_a(Opcode::JmpIfFalse, 6), ins_a(Opcode::PushConst, 1),
       ins(Opcode::Return), ins_a(Opcode::PushConst, 2),
       ins_a(Opcode::PushConst, 3),
       ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Concat), 2),
       ins(Opcode::Return)},
      {{slot(RegionSlotBank::State, 0), 0}}, 1);
}

bool test_mixed_children_rejected_before_combine() {
  const Value exact = payload::make_string_value("exact");
  const Value left = payload::make_string_value(std::string(256, 'a'));
  const Value right = payload::make_string_value(std::string(257, 'b'));
  BoundedRegionSegment segment = unary_segment(
      ValueTag::String, mixed_base(exact, left, right),
      load_phase(RegionSlotBank::Result, 0), {-2, -1});
  segment.base_predicate = phase(
      {Value::from_int(1)},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::PushConst, 0), ins(Opcode::Le),
       ins(Opcode::Return)},
      {{slot(RegionSlotBank::State, 0), 0}}, 1);
  const BytecodeProgram program = invocation(std::move(segment), 2);
  if (!valid(program, "mixed child actual tags")) return false;
  const Observed actual = probe(program, 16);
  return check(actual.is_error && actual.error == "TypeError",
               "mixed exact/fallback child results reached combine");
}

bool test_combine_fallback_must_match_child_tag() {
  const Value exact = payload::make_string_value(std::string(512, 'q'));
  const Value suffix = payload::make_string_value("z");
  const RegionPhase combine = phase(
      {suffix},
      {ins_a(Opcode::Load, 0), ins_a(Opcode::PushConst, 0),
       ins_ab(Opcode::CallBuiltin, static_cast<int>(BuiltinId::Concat), 2),
       ins(Opcode::Return)},
      {{slot(RegionSlotBank::Result, 0), 0}}, 1);
  const BytecodeProgram program = invocation(
      unary_segment(ValueTag::String, constant_phase(exact), combine), 1);
  if (!valid(program, "combine-produced fallback")) return false;
  const Observed actual = probe(program, 8);
  return check(actual.is_error && actual.error == "TypeError",
               "combine fallback did not reject an exact child tag mismatch");
}

}  // namespace

int main() {
  payload::clear();
  cudaDeviceProp properties{};
  std::string device_error;
  if (!check(gagp::select_gpu_device(properties, device_error),
             "GPU selection failed: " + device_error)) {
    return 1;
  }
  if (!test_public_capture_decoder()) return 1;
  if (!test_exact_string_and_fuel()) return 1;
  if (!test_string_fallback()) return 1;
  if (!test_compact_lists_and_index()) return 1;
  if (!test_memoized_identity_fallback()) return 1;
  if (!test_exact_payload_memo_roots()) return 1;
  if (!test_physical_frame_capacity()) return 1;
  if (!test_physical_memo_capacity()) return 1;
  if (!test_maximum_state_request_preparation_shape()) return 1;
  if (!test_mixed_children_rejected_before_combine()) return 1;
  if (!test_combine_fallback_must_match_child_tag()) return 1;
  std::cout << "gagp_test_bounded_region_gpu_results: OK\n";
  return 0;
}
