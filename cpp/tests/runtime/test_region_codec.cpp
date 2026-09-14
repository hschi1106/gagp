#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/cli/codec.hpp"
#include "gagp/cli/region_codec.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace {

using namespace gagp;
using gagp::cli_detail::JsonValue;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

template <typename Function>
bool rejects(Function&& function, const std::string& message) {
  try {
    function();
  } catch (const std::exception&) {
    return true;
  }
  std::cerr << "FAIL: " << message << "\n";
  return false;
}

JsonValue object() { JsonValue v; v.kind = JsonValue::Kind::Object; return v; }
JsonValue array() { JsonValue v; v.kind = JsonValue::Kind::Array; return v; }
JsonValue string(std::string s) {
  JsonValue v; v.kind = JsonValue::Kind::String; v.string_v = std::move(s); return v;
}
JsonValue number(double n) { JsonValue v; v.kind = JsonValue::Kind::Number; v.number_v = n; return v; }

Instr instruction(Opcode op, int a = 0, bool has_a = false) {
  return Instr{op, a, 0, has_a, false};
}

RegionPhase constant_phase(Value value) {
  RegionPhase phase;
  phase.program.consts = {value};
  phase.program.code = {
      instruction(Opcode::PushConst, 0, true),
      instruction(Opcode::Return),
  };
  phase.program.instruction_fuel = {
      0, static_cast<std::uint32_t>(std::numeric_limits<int>::max())};
  return phase;
}

RegionStateTransition offset_transition(std::int64_t offset) {
  RegionStateTransition transition;
  transition.kind = RegionTransitionKind::CoordinateOffset;
  transition.offset = offset;
  return transition;
}

RegionStateTransition expression_transition(std::uint32_t expression) {
  RegionStateTransition transition;
  transition.kind = RegionTransitionKind::Expression;
  transition.expression = expression;
  return transition;
}

BoundedRegionSegment region_segment() {
  BoundedRegionSegment segment;
  RegionPlan& plan = segment.plan;
  plan.state_types = {ValueTag::Int, ValueTag::FloatList};
  plan.result_type = ValueTag::StringList;
  plan.parameter_types = {
      ValueTag::Int, ValueTag::Float, ValueTag::Bool, ValueTag::Char,
      ValueTag::String, ValueTag::IntList, ValueTag::FloatList,
      ValueTag::StringList};
  plan.preparations = {
      {ValueTag::Float, RegionPreparationKind::Identity},
      {ValueTag::Char, RegionPreparationKind::Identity},
      {ValueTag::String, RegionPreparationKind::Identity},
      {ValueTag::IntList, RegionPreparationKind::Identity},
  };
  plan.request_expression_types = {ValueTag::Int, ValueTag::FloatList};
  plan.bound_operand_count = 1;
  plan.requests = {{{offset_transition(std::numeric_limits<std::int64_t>::min()),
                     expression_transition(1)}}};
  plan.coordinate_slots = {0};
  plan.coordinate_rank = {{0, 1}};
  RegionBound lower;
  lower.kind = RegionBoundKind::Operand;
  lower.operand = 2;
  RegionBound upper;
  upper.literal = std::numeric_limits<std::int64_t>::max();
  plan.coordinate_domains = {{lower, upper}};
  plan.limits.frames = std::numeric_limits<std::uint32_t>::max();
  plan.limits.entry_fuel = static_cast<std::uint32_t>(std::numeric_limits<int>::max());

  segment.parameter_locals = {0, 1, 2, 3, 4, 5, 6, 7};
  const Value nested = payload::make_string_value("nested");
  const Value result = payload::make_string_list_value({nested});
  segment.boundary = constant_phase(result);
  segment.base_predicate = constant_phase(Value::from_bool(false));
  segment.base_body = constant_phase(result);
  segment.preparations = {
      constant_phase(Value::from_float(-1.25)),
      constant_phase(Value::from_char(0x10ffff)),
      constant_phase(payload::make_string_value("payload")),
      constant_phase(payload::make_int_list_value({})),
  };
  segment.request_expressions = {
      constant_phase(Value::from_int(std::numeric_limits<std::int64_t>::min())),
      constant_phase(payload::make_float_list_value({})),
  };
  segment.combine = constant_phase(result);
  return segment;
}

bool test_round_trip_and_exact_constants() {
  const BoundedRegionSegment segment = region_segment();
  const JsonValue encoded = cli_detail::encode_bounded_region_segment(segment);
  const BoundedRegionSegment decoded =
      cli_detail::decode_bounded_region_segment(encoded);
  const JsonValue reencoded = cli_detail::encode_bounded_region_segment(decoded);
  if (!check(evo::grammar::canonical_json(encoded) ==
                 evo::grammar::canonical_json(reencoded),
             "bounded region segment has a canonical round trip")) return false;

  const JsonValue& plan = encoded.object_v.at("plan");
  const JsonValue& transition =
      plan.object_v.at("requests").array_v[0]
          .object_v.at("states").array_v[0];
  if (!check(transition.object_v.at("offset").string_v ==
                 std::to_string(std::numeric_limits<std::int64_t>::min()),
             "INT64_MIN offset is encoded as an exact decimal string")) return false;
  const JsonValue& upper = plan.object_v.at("coordinate_domains").array_v[0]
                               .object_v.at("upper");
  if (!check(upper.object_v.at("literal").string_v ==
                 std::to_string(std::numeric_limits<std::int64_t>::max()),
             "INT64_MAX bound is encoded as an exact decimal string")) return false;
  return check(decoded.preparations[3].program.consts[0].tag == ValueTag::IntList &&
                   Value::container_len(decoded.preparations[3].program.consts[0]) == 0 &&
                   decoded.request_expressions[1].program.consts[0].tag ==
                       ValueTag::FloatList &&
                   Value::container_len(
                       decoded.request_expressions[1].program.consts[0]) == 0,
               "typed empty phase constants retain their exact list tags");
}

bool test_strict_plan_rejections() {
  const JsonValue encoded = cli_detail::encode_region_plan(region_segment().plan);
  JsonValue bad = encoded;
  bad.object_v["unknown"] = JsonValue{};
  if (!rejects([&] { (void)cli_detail::decode_region_plan(bad); },
               "unknown plan field is rejected")) return false;
  bad = encoded;
  bad.object_v.erase("memoized");
  if (!rejects([&] { (void)cli_detail::decode_region_plan(bad); },
               "missing plan field is rejected")) return false;
  bad = encoded;
  bad.object_v["progress"] = string("numeric_cast_0");
  if (!rejects([&] { (void)cli_detail::decode_region_plan(bad); },
               "unknown enum string is rejected")) return false;
  bad = encoded;
  bad.object_v["version"] = number(1.5);
  if (!rejects([&] { (void)cli_detail::decode_region_plan(bad); },
               "fractional uint32 field is rejected")) return false;
  bad = encoded;
  bad.object_v["bound_operand_count"] = number(
      static_cast<double>(std::numeric_limits<std::uint32_t>::max()) + 1.0);
  if (!rejects([&] { (void)cli_detail::decode_region_plan(bad); },
               "out-of-range uint32 field is rejected")) return false;

  bad = encoded;
  JsonValue& offset = bad.object_v["requests"].array_v[0]
                          .object_v["states"].array_v[0].object_v["offset"];
  offset = string("01");
  if (!rejects([&] { (void)cli_detail::decode_region_plan(bad); },
               "noncanonical int64 string is rejected")) return false;
  bad = encoded;
  bad.object_v["requests"].array_v[0]
      .object_v["states"].array_v[0].object_v["offset"] = number(-1);
  if (!rejects([&] { (void)cli_detail::decode_region_plan(bad); },
               "numeric int64 field is rejected")) return false;
  bad = encoded;
  bad.object_v["requests"].array_v[0]
      .object_v["states"].array_v[0].object_v["offset"] =
      string("9223372036854775808");
  if (!rejects([&] { (void)cli_detail::decode_region_plan(bad); },
               "overflowing int64 string is rejected")) return false;
  bad = encoded;
  bad.object_v["requests"].array_v[0]
      .object_v["states"].array_v[0].object_v["offset"] = string("1");
  if (!rejects([&] { (void)cli_detail::decode_region_plan(bad); },
               "decoded plans still enforce progress proof")) return false;

  bad = encoded;
  const JsonValue preparation = bad.object_v["preparations"].array_v.front();
  bad.object_v["preparations"].array_v.resize(
      gagp::kRegionPreparationCapacity + 1, preparation);
  if (!rejects([&] { (void)cli_detail::decode_region_plan(bad); },
               "array capacity is checked before decoding entries")) return false;
  bad = encoded;
  bad.object_v["requests"].array_v.clear();
  return rejects([&] { (void)cli_detail::decode_region_plan(bad); },
                 "invalid request arity is rejected");
}

bool test_phase_and_segment_rejections() {
  const JsonValue encoded = cli_detail::encode_bounded_region_segment(region_segment());
  JsonValue bad = encoded;
  bad.object_v["extra"] = JsonValue{};
  if (!rejects([&] { (void)cli_detail::decode_bounded_region_segment(bad); },
               "unknown segment field is rejected")) return false;
  bad = encoded;
  bad.object_v["parameter_locals"].array_v.pop_back();
  if (!rejects([&] { (void)cli_detail::decode_bounded_region_segment(bad); },
               "parameter arity mismatch is rejected")) return false;
  bad = encoded;
  bad.object_v["boundary"] = JsonValue{};
  if (!rejects([&] { (void)cli_detail::decode_bounded_region_segment(bad); },
               "coordinate segment requires boundary phase")) return false;

  bad = encoded;
  JsonValue& program = bad.object_v["base_predicate"].object_v["program"];
  program.object_v["instruction_fuel"].array_v[0] = number(-1);
  if (!rejects([&] { (void)cli_detail::decode_bounded_region_segment(bad); },
               "negative phase fuel is rejected")) return false;
  bad = encoded;
  bad.object_v["base_predicate"].object_v["program"]
      .object_v["instruction_fuel"].array_v[0] =
      number(static_cast<double>(std::numeric_limits<int>::max()) + 1.0);
  if (!rejects([&] { (void)cli_detail::decode_bounded_region_segment(bad); },
               "phase fuel above INT_MAX is rejected")) return false;
  bad = encoded;
  bad.object_v["base_predicate"].object_v["program"]
      .object_v["instruction_fuel"].array_v.pop_back();
  if (!rejects([&] { (void)cli_detail::decode_bounded_region_segment(bad); },
               "phase fuel arity mismatch is rejected")) return false;
  bad = encoded;
  bad.object_v["base_predicate"].object_v["program"]
      .object_v["var2idx"].array_v.push_back(JsonValue{});
  if (!rejects([&] { (void)cli_detail::decode_bounded_region_segment(bad); },
               "nonempty legacy phase map is rejected")) return false;
  bad = encoded;
  bad.object_v["base_predicate"].object_v["program"]
      .object_v["n_locals"] = number(-1);
  if (!rejects([&] { (void)cli_detail::decode_bounded_region_segment(bad); },
               "negative phase local count is rejected")) return false;

  bad = encoded;
  JsonValue binding = object();
  binding.object_v["bank"] = string("unknown");
  binding.object_v["slot"] = number(0);
  binding.object_v["local"] = number(0);
  bad.object_v["base_predicate"].object_v["bindings"].array_v.push_back(binding);
  if (!rejects([&] { (void)cli_detail::decode_bounded_region_segment(bad); },
               "unknown binding bank is rejected")) return false;

  bad = encoded;
  binding.object_v["bank"] = string("state");
  bad.object_v["base_predicate"].object_v["bindings"].array_v.assign(
      50, binding);
  return rejects([&] { (void)cli_detail::decode_bounded_region_segment(bad); },
                 "phase bindings beyond the aggregate 49-slot cap are rejected");
}

JsonValue legacy_int(std::int64_t value) {
  JsonValue out = object();
  out.object_v["type"] = string("int");
  out.object_v["value"] = number(static_cast<double>(value));
  return out;
}

JsonValue legacy_instruction(const char* op, int a = 0, bool has_a = false) {
  JsonValue out = object();
  out.object_v["op"] = string(op);
  if (has_a) out.object_v["a"] = number(a);
  return out;
}

bool test_whole_program_decode() {
  JsonValue program = object();
  program.object_v["n_locals"] = number(8);
  JsonValue constants = array();
  constants.array_v = {legacy_int(4), legacy_int(0)};
  program.object_v["consts"] = std::move(constants);
  JsonValue code = array();
  code.array_v = {
      legacy_instruction("PUSH_CONST", 0, true),
      legacy_instruction("EMPTY_LIST", 2, true),
      legacy_instruction("PUSH_CONST", 1, true),
      legacy_instruction("BOUNDED_REGION", 0, true),
      legacy_instruction("RETURN"),
  };
  program.object_v["code"] = std::move(code);
  JsonValue segments = object();
  JsonValue bounded = array();
  bounded.array_v.push_back(
      cli_detail::encode_bounded_region_segment(region_segment()));
  segments.object_v["bounded_region"] = std::move(bounded);
  program.object_v["segments"] = std::move(segments);

  const BytecodeProgram decoded = cli_detail::decode_program(program);
  return check(decoded.bounded_region_segments.size() == 1 &&
                   decoded.code[3].op == Opcode::BoundedRegion,
               "decode_program accepts and verifies opcode 28 with a region segment");
}

}  // namespace

int main() {
  if (!test_round_trip_and_exact_constants()) return 1;
  if (!test_strict_plan_rejections()) return 1;
  if (!test_phase_and_segment_rejections()) return 1;
  if (!test_whole_program_decode()) return 1;
  std::cout << "gagp_test_region_codec: OK\n";
  return 0;
}
