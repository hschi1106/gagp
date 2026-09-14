#include "gagp/serialization/region_plan_json.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace gagp::serialization {
namespace {

using gagp::cli_detail::JsonValue;
using Json = JsonValue;

using gagp::cli_detail::require_object_field;
using gagp::cli_detail::require_string;

Json object() { Json value; value.kind = Json::Kind::Object; return value; }
Json array() { Json value; value.kind = Json::Kind::Array; return value; }
Json string(std::string text) {
  Json value; value.kind = Json::Kind::String; value.string_v = std::move(text); return value;
}
Json number(double n) { Json value; value.kind = Json::Kind::Number; value.number_v = n; return value; }
Json boolean(bool b) { Json value; value.kind = Json::Kind::Bool; value.bool_v = b; return value; }
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

bool bool_value(const Json& value, const char* context) {
  if (value.kind != Json::Kind::Bool) {
    throw std::runtime_error(std::string(context) + " must be bool");
  }
  return value.bool_v;
}

std::int64_t int64_string(const Json& value, const char* context) {
  if (value.kind != Json::Kind::String || value.string_v.empty()) {
    throw std::runtime_error(std::string(context) + " must be a canonical decimal string");
  }
  std::int64_t parsed = 0;
  const char* begin = value.string_v.data();
  const char* end = begin + value.string_v.size();
  const auto result = std::from_chars(begin, end, parsed, 10);
  if (result.ec != std::errc{} || result.ptr != end ||
      std::to_string(parsed) != value.string_v) {
    throw std::runtime_error(std::string(context) + " must be a canonical int64 decimal string");
  }
  return parsed;
}

const char* type_name(ValueTag type) {
  switch (type) {
    case ValueTag::Int: return "Int";
    case ValueTag::Float: return "Float";
    case ValueTag::Bool: return "Bool";
    case ValueTag::Char: return "Char";
    case ValueTag::String: return "String";
    case ValueTag::IntList: return "IntList";
    case ValueTag::FloatList: return "FloatList";
    case ValueTag::StringList: return "StringList";
    default: throw std::invalid_argument("region codec requires a public ValueTag");
  }
}

ValueTag decode_type(const Json& value) {
  const std::string name = require_string(value, "value type");
  if (name == "Int") return ValueTag::Int;
  if (name == "Float") return ValueTag::Float;
  if (name == "Bool") return ValueTag::Bool;
  if (name == "Char") return ValueTag::Char;
  if (name == "String") return ValueTag::String;
  if (name == "IntList") return ValueTag::IntList;
  if (name == "FloatList") return ValueTag::FloatList;
  if (name == "StringList") return ValueTag::StringList;
  throw std::runtime_error("unknown region value type: " + name);
}

Json encode_types(const std::vector<ValueTag>& types) {
  Json out = array();
  for (ValueTag type : types) out.array_v.push_back(string(type_name(type)));
  return out;
}

std::vector<ValueTag> decode_types(const Json& value, std::size_t capacity,
                                   const char* context) {
  const auto& raw = elements(value, capacity, context);
  std::vector<ValueTag> out;
  out.reserve(raw.size());
  for (const Json& item : raw) out.push_back(decode_type(item));
  return out;
}

const char* endpoint_name(WindowEndpointKind kind) {
  switch (kind) {
    case WindowEndpointKind::Begin: return "begin";
    case WindowEndpointKind::End: return "end";
    case WindowEndpointKind::InteriorCut: return "interior_cut";
  }
  throw std::invalid_argument("invalid window endpoint kind");
}

WindowEndpointKind decode_endpoint_kind(const Json& value) {
  const std::string name = require_string(value, "window endpoint kind");
  if (name == "begin") return WindowEndpointKind::Begin;
  if (name == "end") return WindowEndpointKind::End;
  if (name == "interior_cut") return WindowEndpointKind::InteriorCut;
  throw std::runtime_error("unknown window endpoint kind: " + name);
}

Json encode_endpoint(const WindowEndpoint& endpoint) {
  Json out = object();
  out.object_v["kind"] = string(endpoint_name(endpoint.kind));
  out.object_v["cut"] = number(endpoint.cut);
  return out;
}

WindowEndpoint decode_endpoint(const Json& value) {
  exact_object(value, {"kind", "cut"}, "window endpoint");
  return {decode_endpoint_kind(require_object_field(value, "kind")),
          uint32_number(require_object_field(value, "cut"), "window endpoint cut")};
}

Json encode_window(const SequenceWindow& window) {
  Json out = object();
  out.object_v["begin"] = encode_endpoint(window.begin);
  out.object_v["end"] = encode_endpoint(window.end);
  return out;
}

SequenceWindow decode_window(const Json& value) {
  exact_object(value, {"begin", "end"}, "sequence window");
  return {decode_endpoint(require_object_field(value, "begin")),
          decode_endpoint(require_object_field(value, "end"))};
}

const char* bound_kind_name(RegionBoundKind kind) {
  switch (kind) {
    case RegionBoundKind::Literal: return "literal";
    case RegionBoundKind::Operand: return "operand";
  }
  throw std::invalid_argument("invalid region bound kind");
}

RegionBoundKind decode_bound_kind(const Json& value) {
  const std::string name = require_string(value, "region bound kind");
  if (name == "literal") return RegionBoundKind::Literal;
  if (name == "operand") return RegionBoundKind::Operand;
  throw std::runtime_error("unknown region bound kind: " + name);
}

Json encode_bound(const RegionBound& bound) {
  Json out = object();
  out.object_v["kind"] = string(bound_kind_name(bound.kind));
  out.object_v["literal"] = string(std::to_string(bound.literal));
  out.object_v["operand"] = number(bound.operand);
  return out;
}

RegionBound decode_bound(const Json& value) {
  exact_object(value, {"kind", "literal", "operand"}, "region bound");
  RegionBound out;
  out.kind = decode_bound_kind(require_object_field(value, "kind"));
  out.literal = int64_string(require_object_field(value, "literal"), "region bound literal");
  out.operand = uint32_number(require_object_field(value, "operand"), "region bound operand");
  return out;
}

const char* preparation_kind_name(RegionPreparationKind kind) {
  switch (kind) {
    case RegionPreparationKind::Identity: return "identity";
    case RegionPreparationKind::InteriorCut: return "interior_cut";
  }
  throw std::invalid_argument("invalid region preparation kind");
}

RegionPreparationKind decode_preparation_kind(const Json& value) {
  const std::string name = require_string(value, "region preparation kind");
  if (name == "identity") return RegionPreparationKind::Identity;
  if (name == "interior_cut") return RegionPreparationKind::InteriorCut;
  throw std::runtime_error("unknown region preparation kind: " + name);
}

const char* transition_kind_name(RegionTransitionKind kind) {
  switch (kind) {
    case RegionTransitionKind::CopyState: return "copy_state";
    case RegionTransitionKind::CoordinateOffset: return "coordinate_offset";
    case RegionTransitionKind::SequenceWindow: return "sequence_window";
    case RegionTransitionKind::Expression: return "expression";
  }
  throw std::invalid_argument("invalid region transition kind");
}

RegionTransitionKind decode_transition_kind(const Json& value) {
  const std::string name = require_string(value, "region transition kind");
  if (name == "copy_state") return RegionTransitionKind::CopyState;
  if (name == "coordinate_offset") return RegionTransitionKind::CoordinateOffset;
  if (name == "sequence_window") return RegionTransitionKind::SequenceWindow;
  if (name == "expression") return RegionTransitionKind::Expression;
  throw std::runtime_error("unknown region transition kind: " + name);
}

const char* duplicate_name(DuplicatePolicy policy) {
  switch (policy) {
    case DuplicatePolicy::Reject: return "reject";
    case DuplicatePolicy::Allow: return "allow";
  }
  throw std::invalid_argument("invalid duplicate policy");
}

DuplicatePolicy decode_duplicate(const Json& value) {
  const std::string name = require_string(value, "duplicate policy");
  if (name == "reject") return DuplicatePolicy::Reject;
  if (name == "allow") return DuplicatePolicy::Allow;
  throw std::runtime_error("unknown duplicate policy: " + name);
}

const char* endpoint_policy_name(DomainEndpoint endpoint) {
  switch (endpoint) {
    case DomainEndpoint::Exclusive: return "exclusive";
    case DomainEndpoint::Inclusive: return "inclusive";
  }
  throw std::invalid_argument("invalid domain endpoint policy");
}

DomainEndpoint decode_endpoint_policy(const Json& value) {
  const std::string name = require_string(value, "domain endpoint policy");
  if (name == "exclusive") return DomainEndpoint::Exclusive;
  if (name == "inclusive") return DomainEndpoint::Inclusive;
  throw std::runtime_error("unknown domain endpoint policy: " + name);
}

const char* progress_name(RegionProgressKind progress) {
  switch (progress) {
    case RegionProgressKind::Coordinates: return "coordinates";
    case RegionProgressKind::SequenceWindows: return "sequence_windows";
  }
  throw std::invalid_argument("invalid region progress kind");
}

RegionProgressKind decode_progress(const Json& value) {
  const std::string name = require_string(value, "region progress kind");
  if (name == "coordinates") return RegionProgressKind::Coordinates;
  if (name == "sequence_windows") return RegionProgressKind::SequenceWindows;
  throw std::runtime_error("unknown region progress kind: " + name);
}

Json encode_transition(const RegionStateTransition& transition) {
  Json out = object();
  out.object_v["kind"] = string(transition_kind_name(transition.kind));
  out.object_v["source_state"] = number(transition.source_state);
  out.object_v["offset"] = string(std::to_string(transition.offset));
  out.object_v["window"] = encode_window(transition.window);
  out.object_v["expression"] = number(transition.expression);
  return out;
}

RegionStateTransition decode_transition(const Json& value) {
  exact_object(value, {"kind", "source_state", "offset", "window", "expression"},
               "region state transition");
  RegionStateTransition out;
  out.kind = decode_transition_kind(require_object_field(value, "kind"));
  out.source_state = uint32_number(require_object_field(value, "source_state"),
                                   "transition source_state");
  out.offset = int64_string(require_object_field(value, "offset"), "transition offset");
  out.window = decode_window(require_object_field(value, "window"));
  out.expression = uint32_number(require_object_field(value, "expression"),
                                 "transition expression");
  return out;
}


}  // namespace

JsonValue encode_region_plan(const RegionPlan& plan) {
  validate_region_plan(plan);
  Json out = object();
  out.object_v["version"] = number(plan.version);
  out.object_v["state_types"] = encode_types(plan.state_types);
  out.object_v["result_type"] = string(type_name(plan.result_type));
  out.object_v["parameter_types"] = encode_types(plan.parameter_types);
  Json preparations = array();
  for (const RegionPreparation& preparation : plan.preparations) {
    Json item = object();
    item.object_v["type"] = string(type_name(preparation.type));
    item.object_v["kind"] = string(preparation_kind_name(preparation.kind));
    preparations.array_v.push_back(std::move(item));
  }
  out.object_v["preparations"] = std::move(preparations);
  out.object_v["request_expression_types"] = encode_types(plan.request_expression_types);
  out.object_v["bound_operand_count"] = number(plan.bound_operand_count);
  Json requests = array();
  for (const RegionRequest& request : plan.requests) {
    Json item = object();
    Json states = array();
    for (const RegionStateTransition& transition : request.states) {
      states.array_v.push_back(encode_transition(transition));
    }
    item.object_v["states"] = std::move(states);
    requests.array_v.push_back(std::move(item));
  }
  out.object_v["requests"] = std::move(requests);
  Json limits = object();
  limits.object_v["frames"] = number(plan.limits.frames);
  limits.object_v["cells"] = number(plan.limits.cells);
  limits.object_v["entry_fuel"] = number(plan.limits.entry_fuel);
  out.object_v["limits"] = std::move(limits);
  out.object_v["memoized"] = boolean(plan.memoized);
  out.object_v["duplicate_policy"] = string(duplicate_name(plan.duplicate_policy));
  out.object_v["progress"] = string(progress_name(plan.progress));
  Json slots = array();
  for (std::uint32_t slot : plan.coordinate_slots) slots.array_v.push_back(number(slot));
  out.object_v["coordinate_slots"] = std::move(slots);
  Json rank = array();
  for (const RankAxis& axis : plan.coordinate_rank) {
    Json item = object();
    item.object_v["coordinate"] = number(axis.coordinate);
    item.object_v["direction"] = number(axis.direction);
    rank.array_v.push_back(std::move(item));
  }
  out.object_v["coordinate_rank"] = std::move(rank);
  Json domains = array();
  for (const RegionCoordinateDomain& domain : plan.coordinate_domains) {
    Json item = object();
    item.object_v["lower"] = encode_bound(domain.lower);
    item.object_v["upper"] = encode_bound(domain.upper);
    domains.array_v.push_back(std::move(item));
  }
  out.object_v["coordinate_domains"] = std::move(domains);
  out.object_v["coordinate_endpoint"] = string(endpoint_policy_name(plan.coordinate_endpoint));
  out.object_v["sequence_state"] = number(plan.sequence_state);
  return out;
}

RegionPlan decode_region_plan(const JsonValue& value) {
  exact_object(value, {"version", "state_types", "result_type", "parameter_types",
                       "preparations", "request_expression_types", "bound_operand_count",
                       "requests", "limits", "memoized", "duplicate_policy", "progress",
                       "coordinate_slots", "coordinate_rank", "coordinate_domains",
                       "coordinate_endpoint", "sequence_state"}, "region plan");
  RegionPlan out;
  out.version = uint32_number(require_object_field(value, "version"), "region plan version");
  out.state_types = decode_types(require_object_field(value, "state_types"),
                                 kRecurrenceCoordinateCapacity, "region state_types");
  out.result_type = decode_type(require_object_field(value, "result_type"));
  out.parameter_types = decode_types(require_object_field(value, "parameter_types"),
                                     kRegionParameterCapacity, "region parameter_types");
  const auto& preparations = elements(require_object_field(value, "preparations"),
                                      kRegionPreparationCapacity, "region preparations");
  out.preparations.reserve(preparations.size());
  for (const Json& item : preparations) {
    exact_object(item, {"type", "kind"}, "region preparation");
    out.preparations.push_back({decode_type(require_object_field(item, "type")),
                                decode_preparation_kind(require_object_field(item, "kind"))});
  }
  out.request_expression_types = decode_types(
      require_object_field(value, "request_expression_types"),
      kRecurrenceCoordinateCapacity * kRecurrenceRequestCapacity,
      "region request_expression_types");
  out.bound_operand_count = uint32_number(require_object_field(value, "bound_operand_count"),
                                          "region bound_operand_count");
  if (out.bound_operand_count > kRegionBoundOperandCapacity) {
    throw std::runtime_error("region bound_operand_count exceeds capacity");
  }
  const auto& requests = elements(require_object_field(value, "requests"),
                                  kRecurrenceRequestCapacity, "region requests");
  out.requests.reserve(requests.size());
  for (const Json& request : requests) {
    exact_object(request, {"states"}, "region request");
    const auto& states = elements(require_object_field(request, "states"),
                                  kRecurrenceCoordinateCapacity, "region request states");
    RegionRequest decoded;
    decoded.states.reserve(states.size());
    for (const Json& transition : states) decoded.states.push_back(decode_transition(transition));
    out.requests.push_back(std::move(decoded));
  }
  const Json& limits = require_object_field(value, "limits");
  exact_object(limits, {"frames", "cells", "entry_fuel"}, "region limits");
  out.limits.frames = uint32_number(require_object_field(limits, "frames"), "region frames");
  out.limits.cells = uint32_number(require_object_field(limits, "cells"), "region cells");
  out.limits.entry_fuel = uint32_number(require_object_field(limits, "entry_fuel"),
                                        "region entry_fuel");
  out.memoized = bool_value(require_object_field(value, "memoized"), "region memoized");
  out.duplicate_policy = decode_duplicate(require_object_field(value, "duplicate_policy"));
  out.progress = decode_progress(require_object_field(value, "progress"));
  const auto& slots = elements(require_object_field(value, "coordinate_slots"),
                               kRecurrenceCoordinateCapacity, "region coordinate_slots");
  for (const Json& slot : slots) out.coordinate_slots.push_back(uint32_number(slot, "coordinate slot"));
  const auto& rank = elements(require_object_field(value, "coordinate_rank"),
                              kRecurrenceCoordinateCapacity, "region coordinate_rank");
  for (const Json& item : rank) {
    exact_object(item, {"coordinate", "direction"}, "coordinate rank axis");
    out.coordinate_rank.push_back({
        uint32_number(require_object_field(item, "coordinate"), "rank coordinate"),
        int_number(require_object_field(item, "direction"), "rank direction")});
  }
  const auto& domains = elements(require_object_field(value, "coordinate_domains"),
                                 kRecurrenceCoordinateCapacity, "region coordinate_domains");
  for (const Json& item : domains) {
    exact_object(item, {"lower", "upper"}, "coordinate domain");
    out.coordinate_domains.push_back({decode_bound(require_object_field(item, "lower")),
                                      decode_bound(require_object_field(item, "upper"))});
  }
  out.coordinate_endpoint = decode_endpoint_policy(require_object_field(value, "coordinate_endpoint"));
  out.sequence_state = uint32_number(require_object_field(value, "sequence_state"),
                                     "region sequence_state");
  validate_region_plan(out);
  return out;
}


}  // namespace gagp::serialization
