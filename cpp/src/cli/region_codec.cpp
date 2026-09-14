#include "gagp/cli/region_codec.hpp"

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#include "gagp/cli/codec.hpp"
#include "gagp/core/bytecode_verify.hpp"
#include "gagp/evolution/grammar/values.hpp"

namespace gagp::cli_detail {
namespace {

using Json = JsonValue;

Json object() { Json value; value.kind = Json::Kind::Object; return value; }
Json array() { Json value; value.kind = Json::Kind::Array; return value; }
Json string(std::string text) {
  Json value; value.kind = Json::Kind::String; value.string_v = std::move(text); return value;
}
Json number(double n) { Json value; value.kind = Json::Kind::Number; value.number_v = n; return value; }
Json null() { return Json{}; }

void exact_object(const Json& value, std::initializer_list<const char*> fields,
                  const char* context) {
  if (value.kind != Json::Kind::Object) {
    throw std::runtime_error(std::string(context) + " must be object");
  }
  std::set<std::string> expected;
  for (const char* field : fields) expected.insert(field);
  if (value.object_v.size() != expected.size()) {
    throw std::runtime_error(std::string(context) + " has missing or unknown fields");
  }
  for (const auto& item : value.object_v) {
    if (expected.count(item.first) == 0) {
      throw std::runtime_error(std::string(context) + " has unknown field: " + item.first);
    }
  }
  for (const std::string& field : expected) {
    if (value.object_v.count(field) == 0) {
      throw std::runtime_error(std::string(context) + " is missing field: " + field);
    }
  }
}

const std::vector<Json>& elements(const Json& value, std::size_t maximum,
                                  const char* context) {
  if (value.kind != Json::Kind::Array) {
    throw std::runtime_error(std::string(context) + " must be array");
  }
  if (value.array_v.size() > maximum) {
    throw std::runtime_error(std::string(context) + " exceeds capacity");
  }
  return value.array_v;
}

std::uint32_t uint32_number(const Json& value, const char* context) {
  if (value.kind != Json::Kind::Number || !std::isfinite(value.number_v) ||
      value.number_v < 0 ||
      value.number_v > static_cast<double>(std::numeric_limits<std::uint32_t>::max()) ||
      std::floor(value.number_v) != value.number_v) {
    throw std::runtime_error(std::string(context) + " must be an integer in uint32 range");
  }
  return static_cast<std::uint32_t>(value.number_v);
}

int int_number(const Json& value, const char* context) {
  if (value.kind != Json::Kind::Number || !std::isfinite(value.number_v) ||
      value.number_v < static_cast<double>(std::numeric_limits<int>::min()) ||
      value.number_v > static_cast<double>(std::numeric_limits<int>::max()) ||
      std::floor(value.number_v) != value.number_v) {
    throw std::runtime_error(std::string(context) + " must be an integer in int range");
  }
  return static_cast<int>(value.number_v);
}

const char* slot_bank_name(RegionSlotBank bank) {
  switch (bank) {
    case RegionSlotBank::State: return "state";
    case RegionSlotBank::Parameter: return "parameter";
    case RegionSlotBank::Prepared: return "prepared";
    case RegionSlotBank::Result: return "result";
    case RegionSlotBank::Measure: return "measure";
  }
  throw std::invalid_argument("invalid region slot bank");
}

RegionSlotBank decode_slot_bank(const Json& value) {
  const std::string name = require_string(value, "region slot bank");
  if (name == "state") return RegionSlotBank::State;
  if (name == "parameter") return RegionSlotBank::Parameter;
  if (name == "prepared") return RegionSlotBank::Prepared;
  if (name == "result") return RegionSlotBank::Result;
  if (name == "measure") return RegionSlotBank::Measure;
  throw std::runtime_error("unknown region slot bank: " + name);
}

Json encode_instruction(const Instr& instruction) {
  Json out = object();
  out.object_v["op"] = string(opcode_name(instruction.op));
  out.object_v["a"] = instruction.has_a ? number(instruction.a) : null();
  out.object_v["b"] = instruction.has_b ? number(instruction.b) : null();
  return out;
}

Json encode_phase_program(const PhaseProgram& program) {
  if (!program.var2idx.empty() || !program.binder_locals.empty()) {
    throw std::invalid_argument("region phase maps must be canonically empty");
  }
  Json out = object();
  out.object_v["n_locals"] = number(program.n_locals);
  Json constants = array();
  for (const Value& value : program.consts) {
    constants.array_v.push_back(evo::grammar::encode_constant(value));
  }
  out.object_v["consts"] = std::move(constants);
  Json code = array();
  for (const Instr& instruction : program.code) code.array_v.push_back(encode_instruction(instruction));
  out.object_v["code"] = std::move(code);
  Json fuel = array();
  for (std::uint32_t cost : program.instruction_fuel) fuel.array_v.push_back(number(cost));
  out.object_v["instruction_fuel"] = std::move(fuel);
  out.object_v["var2idx"] = array();
  out.object_v["binder_locals"] = array();
  return out;
}

PhaseProgram decode_phase_program(const Json& value) {
  exact_object(value, {"n_locals", "consts", "code", "instruction_fuel",
                       "var2idx", "binder_locals"}, "region phase program");
  PhaseProgram out;
  out.n_locals = int_number(require_object_field(value, "n_locals"), "phase n_locals");
  if (out.n_locals < 0) throw std::runtime_error("phase n_locals must be nonnegative");
  const auto& constants = elements(require_object_field(value, "consts"),
                                   static_cast<std::size_t>(std::numeric_limits<int>::max()),
                                   "phase consts");
  out.consts.reserve(constants.size());
  for (const Json& item : constants) out.consts.push_back(evo::grammar::decode_constant(item));

  const auto& raw_code = elements(require_object_field(value, "code"),
                                  static_cast<std::size_t>(std::numeric_limits<int>::max()),
                                  "phase code");
  for (const Json& item : raw_code) {
    exact_object(item, {"op", "a", "b"}, "region phase instruction");
    for (const char* operand_name : {"a", "b"}) {
      const Json& operand = require_object_field(item, operand_name);
      if (operand.kind != Json::Kind::Null) (void)int_number(operand, operand_name);
    }
  }
  out.code = decode_code(require_object_field(value, "code"));

  const auto& raw_fuel = elements(require_object_field(value, "instruction_fuel"),
                                  raw_code.size(), "phase instruction_fuel");
  if (!raw_fuel.empty() && raw_fuel.size() != raw_code.size()) {
    throw std::runtime_error("phase instruction_fuel must be empty or match code size");
  }
  out.instruction_fuel.reserve(raw_fuel.size());
  for (const Json& cost : raw_fuel) {
    const std::uint32_t decoded = uint32_number(cost, "phase instruction fuel");
    if (decoded > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
      throw std::runtime_error("phase instruction fuel exceeds INT_MAX");
    }
    out.instruction_fuel.push_back(decoded);
  }
  if (!elements(require_object_field(value, "var2idx"), 0, "phase var2idx").empty() ||
      !elements(require_object_field(value, "binder_locals"), 0,
                "phase binder_locals").empty()) {
    throw std::runtime_error("region phase maps must be canonically empty");
  }
  return out;
}

Json encode_phase(const RegionPhase& phase) {
  Json out = object();
  out.object_v["program"] = encode_phase_program(phase.program);
  Json bindings = array();
  for (const RegionPhaseBinding& binding : phase.bindings) {
    Json item = object();
    item.object_v["bank"] = string(slot_bank_name(binding.source.bank));
    item.object_v["slot"] = number(binding.source.slot);
    item.object_v["local"] = number(binding.local);
    bindings.array_v.push_back(std::move(item));
  }
  out.object_v["bindings"] = std::move(bindings);
  return out;
}

RegionPhase decode_phase(const Json& value) {
  exact_object(value, {"program", "bindings"}, "region phase");
  RegionPhase out;
  out.program = decode_phase_program(require_object_field(value, "program"));
  const auto& bindings = elements(require_object_field(value, "bindings"),
                                  kRecurrenceCoordinateCapacity + kRegionParameterCapacity +
                                      kRegionPreparationCapacity + kRecurrenceRequestCapacity + 1,
                                  "region phase bindings");
  out.bindings.reserve(bindings.size());
  std::set<int> locals;
  for (const Json& item : bindings) {
    exact_object(item, {"bank", "slot", "local"}, "region phase binding");
    RegionPhaseBinding binding;
    binding.source.bank = decode_slot_bank(require_object_field(item, "bank"));
    binding.source.slot = uint32_number(require_object_field(item, "slot"), "binding slot");
    binding.local = int_number(require_object_field(item, "local"), "binding local");
    if (binding.local < 0 || binding.local >= out.program.n_locals ||
        !locals.insert(binding.local).second) {
      throw std::runtime_error("region phase binding local is out of range or duplicated");
    }
    out.bindings.push_back(binding);
  }
  return out;
}

int synthetic_caller_locals(const BoundedRegionSegment& segment) {
  int caller_n_locals = 0;
  for (int local : segment.parameter_locals) {
    if (local < 0 || local == std::numeric_limits<int>::max()) {
      throw std::runtime_error("region parameter local cannot fit a caller local range");
    }
    if (local >= caller_n_locals) caller_n_locals = local + 1;
  }
  return caller_n_locals;
}

void validate_segment_shape(const BoundedRegionSegment& segment) {
  const BytecodeVerifyResult verified = verify_bounded_region_segment(
      segment, synthetic_caller_locals(segment));
  if (!verified) {
    throw std::runtime_error(std::string("invalid bounded region segment (") +
                             bytecode_verify_code_name(verified.diagnostic.code) +
                             "): " + verified.diagnostic.message);
  }
}

}  // namespace

JsonValue encode_bounded_region_segment(const BoundedRegionSegment& segment) {
  validate_segment_shape(segment);
  Json out = object();
  out.object_v["plan"] = encode_region_plan(segment.plan);
  Json parameters = array();
  for (int local : segment.parameter_locals) parameters.array_v.push_back(number(local));
  out.object_v["parameter_locals"] = std::move(parameters);
  out.object_v["boundary"] = segment.boundary ? encode_phase(*segment.boundary) : null();
  out.object_v["base_predicate"] = encode_phase(segment.base_predicate);
  out.object_v["base_body"] = encode_phase(segment.base_body);
  Json preparations = array();
  for (const RegionPhase& phase : segment.preparations) preparations.array_v.push_back(encode_phase(phase));
  out.object_v["preparations"] = std::move(preparations);
  Json requests = array();
  for (const RegionPhase& phase : segment.request_expressions) requests.array_v.push_back(encode_phase(phase));
  out.object_v["request_expressions"] = std::move(requests);
  out.object_v["combine"] = encode_phase(segment.combine);
  return out;
}

BoundedRegionSegment decode_bounded_region_segment(const JsonValue& value) {
  exact_object(value, {"plan", "parameter_locals", "boundary", "base_predicate",
                       "base_body", "preparations", "request_expressions", "combine"},
               "bounded region segment");
  BoundedRegionSegment out;
  out.plan = decode_region_plan(require_object_field(value, "plan"));
  const auto& parameters = elements(require_object_field(value, "parameter_locals"),
                                    kRegionParameterCapacity, "region parameter_locals");
  out.parameter_locals.reserve(parameters.size());
  for (const Json& local : parameters) {
    const int decoded = int_number(local, "region parameter local");
    if (decoded < 0 || decoded == std::numeric_limits<int>::max()) {
      throw std::runtime_error("region parameter local cannot fit a caller local range");
    }
    out.parameter_locals.push_back(decoded);
  }
  const Json& boundary = require_object_field(value, "boundary");
  if (boundary.kind != Json::Kind::Null) out.boundary = decode_phase(boundary);
  out.base_predicate = decode_phase(require_object_field(value, "base_predicate"));
  out.base_body = decode_phase(require_object_field(value, "base_body"));
  const auto& preparations = elements(require_object_field(value, "preparations"),
                                      kRegionPreparationCapacity, "segment preparations");
  for (const Json& phase : preparations) out.preparations.push_back(decode_phase(phase));
  const auto& requests = elements(require_object_field(value, "request_expressions"),
                                  kRecurrenceCoordinateCapacity * kRecurrenceRequestCapacity,
                                  "segment request_expressions");
  for (const Json& phase : requests) out.request_expressions.push_back(decode_phase(phase));
  out.combine = decode_phase(require_object_field(value, "combine"));

  if (out.parameter_locals.size() != out.plan.parameter_types.size() ||
      out.preparations.size() != out.plan.preparations.size() ||
      out.request_expressions.size() != out.plan.request_expression_types.size()) {
    throw std::runtime_error("bounded region segment phase or parameter arity mismatch");
  }
  if ((out.plan.progress == RegionProgressKind::SequenceWindows) != !out.boundary.has_value()) {
    throw std::runtime_error("bounded region boundary must be null exactly for sequence progress");
  }
  validate_segment_shape(out);
  return out;
}

}  // namespace gagp::cli_detail
