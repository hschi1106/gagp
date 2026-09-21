#include <algorithm>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/fuel_events.hpp"
#include "gagp/evolution/genome_generation.hpp"
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/repro/pack.hpp"
#include "gagp/cli/codec.hpp"
#include "gagp/cli/commands.hpp"
#include "gagp/cli/json.hpp"
#include "gagp/cli/region_codec.hpp"
#include "gagp/cli/grammar_artifact.hpp"
#include "gagp/cli/grammar_population_artifact.hpp"
#include "gagp/cli/options.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace {

using gagp::Value;
using gagp::ValueTag;
using gagp::cli_detail::CliOptions;
using gagp::cli_detail::JsonValue;

std::string json_escape(const std::string& s) {
  std::ostringstream oss;
  static constexpr char kHex[] = "0123456789abcdef";
  for (unsigned char c : s) {
    if (c == '"') {
      oss << "\\\"";
    } else if (c == '\\') {
      oss << "\\\\";
    } else if (c == '\n') {
      oss << "\\n";
    } else if (c == '\r') {
      oss << "\\r";
    } else if (c == '\t') {
      oss << "\\t";
    } else if (c < 0x20) {
      oss << "\\u00" << kHex[c >> 4] << kHex[c & 0x0f];
    } else {
      oss << static_cast<char>(c);
    }
  }
  return oss.str();
}

void write_value_json(std::ostream& out, const Value& v) {
  if (v.tag == ValueTag::Invalid) {
    out << "null";
    return;
  }
  if (v.tag == ValueTag::Bool) {
    out << (v.b ? "true" : "false");
    return;
  }
  if (v.tag == ValueTag::Int) {
    out << v.i;
    return;
  }
  if (v.tag == ValueTag::FallbackToken) {
    out << "null";
    return;
  }
  if (v.tag == ValueTag::String || v.tag == ValueTag::IntList ||
      v.tag == ValueTag::FloatList || v.tag == ValueTag::StringList) {
    out << "null";
    return;
  }
  if (std::isfinite(v.f)) {
    out << std::setprecision(17) << v.f;
    return;
  }
  out << "null";
}

void write_typed_value_json(std::ostream& out, const Value& v);

void write_typed_list_json(std::ostream& out, const char* type, const std::vector<Value>& elems) {
  out << "{\"type\":\"" << type << "\",\"value\":[";
  for (std::size_t i = 0; i < elems.size(); ++i) {
    if (i > 0) out << ",";
    if (std::string(type) == "int_list") {
      if (elems[i].tag != ValueTag::Int) {
        throw std::runtime_error("IntList AST constant contains non-int element");
      }
      out << elems[i].i;
    } else if (std::string(type) == "float_list") {
      if (elems[i].tag != ValueTag::Float) {
        throw std::runtime_error("FloatList AST constant contains non-float element");
      }
      out << std::setprecision(17) << elems[i].f;
    } else {
      if (elems[i].tag != ValueTag::String) {
        throw std::runtime_error("StringList AST constant contains non-string element");
      }
      std::string s;
      if (!gagp::payload::lookup_string(elems[i], &s)) {
        throw std::runtime_error("missing string element payload while writing AST JSON");
      }
      out << "\"" << json_escape(s) << "\"";
    }
  }
  out << "]}";
}

void write_typed_value_json(std::ostream& out, const Value& v) {
  if (v.tag == ValueTag::Bool) {
    out << "{\"type\":\"bool\",\"value\":" << (v.b ? "true" : "false") << "}";
    return;
  }
  if (v.tag == ValueTag::Int) {
    out << "{\"type\":\"int\",\"value\":" << v.i << "}";
    return;
  }
  if (v.tag == ValueTag::Float) {
    out << "{\"type\":\"float\",\"value\":";
    if (std::isfinite(v.f)) {
      out << std::setprecision(17) << v.f;
    } else {
      out << "0";
    }
    out << "}";
    return;
  }
  if (v.tag == ValueTag::Char) {
    std::string s;
    s.push_back(static_cast<char>(v.i & 0xff));
    out << "{\"type\":\"char\",\"value\":\"" << json_escape(s) << "\"}";
    return;
  }
  if (v.tag == ValueTag::String) {
    std::string s;
    if (!gagp::payload::lookup_string(v, &s)) {
      throw std::runtime_error("missing string payload while writing AST JSON");
    }
    out << "{\"type\":\"string\",\"value\":\"" << json_escape(s) << "\"}";
    return;
  }
  if (v.tag == ValueTag::IntList || v.tag == ValueTag::FloatList || v.tag == ValueTag::StringList) {
    std::vector<Value> elems;
    if (!gagp::payload::lookup_list(v, &elems)) {
      throw std::runtime_error("missing list payload while writing AST JSON");
    }
    if (v.tag == ValueTag::IntList) {
      write_typed_list_json(out, "int_list", elems);
    } else if (v.tag == ValueTag::FloatList) {
      write_typed_list_json(out, "float_list", elems);
    } else {
      write_typed_list_json(out, "string_list", elems);
    }
    return;
  }
  throw std::runtime_error("unsupported AST constant value tag");
}

const char* lexical_binding_type_name(gagp::evo::RType type) {
  switch (type) {
    case gagp::evo::RType::Int: return "Int";
    case gagp::evo::RType::Float: return "Float";
    case gagp::evo::RType::Bool: return "Bool";
    case gagp::evo::RType::Char: return "Char";
    case gagp::evo::RType::String: return "String";
    case gagp::evo::RType::IntList: return "IntList";
    case gagp::evo::RType::FloatList: return "FloatList";
    case gagp::evo::RType::StringList: return "StringList";
    case gagp::evo::RType::Any:
    case gagp::evo::RType::Invalid: break;
  }
  throw std::runtime_error("unsupported lexical binding type");
}

const char* region_capture_kind_name(gagp::evo::RegionCaptureKind kind) {
  switch (kind) {
    case gagp::evo::RegionCaptureKind::Lexical: return "lexical";
    case gagp::evo::RegionCaptureKind::Name: return "name";
  }
  throw std::runtime_error("unsupported bounded-region capture kind");
}

const char* region_slot_bank_name(gagp::RegionSlotBank bank) {
  switch (bank) {
    case gagp::RegionSlotBank::State: return "state";
    case gagp::RegionSlotBank::Parameter: return "parameter";
    case gagp::RegionSlotBank::Prepared: return "prepared";
    case gagp::RegionSlotBank::Result: return "result";
    case gagp::RegionSlotBank::Measure: return "measure";
  }
  throw std::runtime_error("unsupported bounded-region slot bank");
}

void write_ast_json(std::ostream& out, const gagp::evo::AstProgram& ast) {
  out << "{";
  out << "\"version\":\"" << json_escape(ast.version) << "\",";
  out << "\"nodes\":[";
  for (std::size_t i = 0; i < ast.nodes.size(); ++i) {
    if (i > 0) out << ",";
    const gagp::evo::AstNode& node = ast.nodes[i];
    out << "{\"kind\":" << static_cast<int>(node.kind)
        << ",\"i0\":" << node.i0
        << ",\"i1\":" << node.i1 << "}";
  }
  out << "],\"names\":[";
  for (std::size_t i = 0; i < ast.names.size(); ++i) {
    if (i > 0) out << ",";
    out << "\"" << json_escape(ast.names[i]) << "\"";
  }
  out << "],\"consts\":[";
  for (std::size_t i = 0; i < ast.consts.size(); ++i) {
    if (i > 0) out << ",";
    write_typed_value_json(out, ast.consts[i]);
  }
  out << "]";
  if (!ast.lexical_regions.empty()) {
    out << ",\"lexical_regions\":[";
    for (std::size_t i = 0; i < ast.lexical_regions.size(); ++i) {
      if (i > 0) out << ",";
      const gagp::evo::LexicalRegion& region = ast.lexical_regions[i];
      if (region.body_argument < 0) {
        throw std::runtime_error("lexical region body_argument must be non-negative");
      }
      out << "{\"node_index\":" << region.node_index
          << ",\"body_argument\":" << region.body_argument
          << ",\"bindings\":[";
      for (std::size_t j = 0; j < region.bindings.size(); ++j) {
        if (j > 0) out << ",";
        const gagp::evo::LexicalBinding& binding = region.bindings[j];
        if (binding.id < 0 || binding.id == std::numeric_limits<int>::max()) {
          throw std::runtime_error("lexical binding id must be in the public non-negative id range");
        }
        out << "{\"id\":" << binding.id
            << ",\"type\":\"" << lexical_binding_type_name(binding.type) << "\"}";
      }
      out << "]}";
    }
    out << "]";
  }
  if (!ast.traversal_specs.empty()) {
    out << ",\"traversal_specs\":[";
    for (std::size_t i = 0; i < ast.traversal_specs.size(); ++i) {
      if (i > 0) out << ",";
      const gagp::evo::TraversalSpec& spec = ast.traversal_specs[i];
      const char* direction = nullptr;
      switch (spec.direction) {
        case gagp::evo::TraversalDirection::Forward: direction = "forward"; break;
        case gagp::evo::TraversalDirection::Reverse: direction = "reverse"; break;
        default: throw std::runtime_error("unsupported traversal direction");
      }
      out << "{\"node_index\":" << spec.node_index
          << ",\"direction\":\"" << direction << "\"}";
    }
    out << "]";
  }
  if (!ast.fuel_specs.empty()) {
    out << ",\"fuel_specs\":[";
    for (std::size_t i = 0; i < ast.fuel_specs.size(); ++i) {
      if (i > 0) out << ",";
      const gagp::evo::NodeFuelSpec& spec = ast.fuel_specs[i];
      out << "{\"node_index\":" << spec.node_index << ",\"charges\":[";
      for (std::size_t j = 0; j < spec.charges.size(); ++j) {
        if (j > 0) out << ",";
        const gagp::evo::FuelCharge& charge = spec.charges[j];
        if (charge.cost > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
          throw std::runtime_error("fuel charge cost exceeds INT_MAX");
        }
        const char* event = gagp::evo::fuel_event_name(charge.event);
        if (std::string(event) == "unknown") {
          throw std::runtime_error("unsupported fuel event");
        }
        out << "{\"event\":\"" << event << "\",\"cost\":" << charge.cost << "}";
      }
      out << "]}";
    }
    out << "]";
  }
  if (!ast.bounded_region_specs.empty()) {
    out << ",\"bounded_region_specs\":[";
    for (std::size_t i = 0; i < ast.bounded_region_specs.size(); ++i) {
      if (i > 0) out << ",";
      const gagp::evo::BoundedRegionSpec& spec = ast.bounded_region_specs[i];
      out << "{\"node_index\":" << spec.node_index
          << ",\"plan\":"
          << gagp::evo::grammar::canonical_json(
                 gagp::cli_detail::encode_region_plan(spec.plan))
          << ",\"parameters\":[";
      for (std::size_t j = 0; j < spec.parameters.size(); ++j) {
        if (j > 0) out << ",";
        const gagp::evo::RegionCapture& capture = spec.parameters[j];
        if (capture.index < 0 || capture.index == std::numeric_limits<int>::max()) {
          throw std::runtime_error("bounded-region capture index is out of range");
        }
        out << "{\"kind\":\"" << region_capture_kind_name(capture.kind)
            << "\",\"index\":" << capture.index << "}";
      }
      out << "],\"phases\":[";
      for (std::size_t j = 0; j < spec.phases.size(); ++j) {
        if (j > 0) out << ",";
        const gagp::evo::RegionAstPhase& phase = spec.phases[j];
        out << "{\"argument\":" << phase.argument << ",\"bindings\":[";
        for (std::size_t k = 0; k < phase.bindings.size(); ++k) {
          if (k > 0) out << ",";
          const gagp::evo::RegionAstBinding& binding = phase.bindings[k];
          if (binding.binder_id < 0 ||
              binding.binder_id == std::numeric_limits<int>::max()) {
            throw std::runtime_error("bounded-region binder id is out of range");
          }
          out << "{\"bank\":\"" << region_slot_bank_name(binding.source.bank)
              << "\",\"slot\":" << binding.source.slot
              << ",\"binder_id\":" << binding.binder_id << "}";
        }
        out << "]}";
      }
      out << "]}";
    }
    out << "]";
  }
  out << "}";
}

bool is_integer_number(double x) {
  const long long i = static_cast<long long>(x);
  return static_cast<double>(i) == x;
}

double canonicalize_metric(double x) {
  if (!std::isfinite(x) || x == 0.0) {
    return x;
  }
  int exp = 0;
  const double frac = std::frexp(x, &exp);
  constexpr int keep_mantissa_bits = 48;
  const double scaled = std::ldexp(frac, keep_mantissa_bits);
  return std::ldexp(std::nearbyint(scaled), exp - keep_mantissa_bits);
}

Value decode_typed_or_raw_value(const JsonValue& v, bool strict_format = false) {
  if (v.kind == JsonValue::Kind::Object) {
    auto it = v.object_v.find("type");
    if (it != v.object_v.end()) {
      if (it->second.kind != JsonValue::Kind::String) {
        throw std::runtime_error("typed value field type must be string");
      }
      if (strict_format &&
          (it->second.string_v == "none" || it->second.string_v == "num_list" || it->second.string_v == "list")) {
        throw std::runtime_error("fitness-cases rejects legacy typed value type: " + it->second.string_v);
      }
      return gagp::cli_detail::decode_typed_value(v);
    }
  }

  if (strict_format) {
    throw std::runtime_error("fitness-cases values must be explicitly typed");
  }

  if (v.kind == JsonValue::Kind::Null) {
    throw std::runtime_error("null is not a public current value");
  }
  if (v.kind == JsonValue::Kind::Bool) {
    return Value::from_bool(v.bool_v);
  }
  if (v.kind == JsonValue::Kind::Number) {
    if (is_integer_number(v.number_v)) {
      return Value::from_int(static_cast<long long>(v.number_v));
    }
    return Value::from_float(v.number_v);
  }
  throw std::runtime_error("unsupported raw value type");
}

gagp::evo::NamedInputs decode_inputs(const JsonValue& raw, bool strict_format = false) {
  if (raw.kind != JsonValue::Kind::Object) {
    throw std::runtime_error("case.inputs must be an object");
  }
  gagp::evo::NamedInputs out;
  for (const auto& kv : raw.object_v) {
    out[kv.first] = decode_typed_or_raw_value(kv.second, strict_format);
  }
  return out;
}

int require_int_field_local(const JsonValue& raw, const char* key, const char* section) {
  auto it = raw.object_v.find(key);
  if (it == raw.object_v.end() || it->second.kind != JsonValue::Kind::Number ||
      !is_integer_number(it->second.number_v)) {
    throw std::runtime_error(std::string("expected integer field: ") + section + "." + key);
  }
  return static_cast<int>(it->second.number_v);
}

std::size_t require_node_index_field_local(const JsonValue& raw, const char* section) {
  const int index = require_int_field_local(raw, "node_index", section);
  if (index < 0) {
    throw std::runtime_error(std::string(section) + ".node_index must be non-negative");
  }
  return static_cast<std::size_t>(index);
}

int require_bounded_nonnegative_int_field_local(const JsonValue& raw,
                                                const char* key,
                                                const char* section,
                                                bool allow_int_max) {
  auto it = raw.object_v.find(key);
  if (it == raw.object_v.end() || it->second.kind != JsonValue::Kind::Number) {
    throw std::runtime_error(std::string("expected integer field: ") + section + "." + key);
  }
  const double value = it->second.number_v;
  const double upper = static_cast<double>(std::numeric_limits<int>::max());
  if (!std::isfinite(value) || std::trunc(value) != value || value < 0.0 ||
      value > upper || (!allow_int_max && value == upper)) {
    throw std::runtime_error(std::string("integer field out of range: ") + section + "." + key);
  }
  return static_cast<int>(value);
}

std::size_t require_bounded_size_field_local(const JsonValue& raw,
                                             const char* key,
                                             const char* section) {
  auto it = raw.object_v.find(key);
  if (it == raw.object_v.end() || it->second.kind != JsonValue::Kind::Number) {
    throw std::runtime_error(std::string("expected integer field: ") + section + "." + key);
  }
  const double value = it->second.number_v;
  const double upper_exclusive =
      std::ldexp(1.0, std::numeric_limits<std::size_t>::digits);
  if (!std::isfinite(value) || std::trunc(value) != value || value < 0.0 ||
      value >= upper_exclusive) {
    throw std::runtime_error(std::string("integer field out of range: ") + section + "." + key);
  }
  return static_cast<std::size_t>(value);
}

gagp::evo::RType decode_lexical_binding_type(const JsonValue& raw,
                                             const char* section) {
  if (raw.kind != JsonValue::Kind::String) {
    throw std::runtime_error(std::string("expected string field: ") + section + ".type");
  }
  if (raw.string_v == "Int") return gagp::evo::RType::Int;
  if (raw.string_v == "Float") return gagp::evo::RType::Float;
  if (raw.string_v == "Bool") return gagp::evo::RType::Bool;
  if (raw.string_v == "Char") return gagp::evo::RType::Char;
  if (raw.string_v == "String") return gagp::evo::RType::String;
  if (raw.string_v == "IntList") return gagp::evo::RType::IntList;
  if (raw.string_v == "FloatList") return gagp::evo::RType::FloatList;
  if (raw.string_v == "StringList") return gagp::evo::RType::StringList;
  throw std::runtime_error(std::string("unknown lexical binding type: ") + raw.string_v);
}

gagp::evo::RegionCaptureKind decode_region_capture_kind(const JsonValue& raw) {
  if (raw.kind != JsonValue::Kind::String) {
    throw std::runtime_error("AST bounded_region_specs.parameters.kind must be a string");
  }
  if (raw.string_v == "lexical") return gagp::evo::RegionCaptureKind::Lexical;
  if (raw.string_v == "name") return gagp::evo::RegionCaptureKind::Name;
  throw std::runtime_error("unknown bounded-region capture kind: " + raw.string_v);
}

gagp::RegionSlotBank decode_region_slot_bank(const JsonValue& raw) {
  if (raw.kind != JsonValue::Kind::String) {
    throw std::runtime_error("AST bounded_region_specs.phases.bindings.bank must be a string");
  }
  if (raw.string_v == "state") return gagp::RegionSlotBank::State;
  if (raw.string_v == "parameter") return gagp::RegionSlotBank::Parameter;
  if (raw.string_v == "prepared") return gagp::RegionSlotBank::Prepared;
  if (raw.string_v == "result") return gagp::RegionSlotBank::Result;
  if (raw.string_v == "measure") return gagp::RegionSlotBank::Measure;
  throw std::runtime_error("unknown bounded-region slot bank: " + raw.string_v);
}

void reject_unknown_bounded_ast_fields(
    const JsonValue& raw, std::initializer_list<const char*> allowed,
    const char* section) {
  for (const auto& field : raw.object_v) {
    bool known = false;
    for (const char* name : allowed) {
      if (field.first == name) {
        known = true;
        break;
      }
    }
    if (!known) {
      throw std::runtime_error(std::string("AST unknown field: ") + section +
                               "." + field.first);
    }
  }
}

gagp::evo::AstProgram decode_ast_json_impl(const JsonValue& raw) {
  if (raw.kind != JsonValue::Kind::Object) {
    throw std::runtime_error("AST JSON must be an object");
  }
  gagp::evo::AstProgram ast;
  auto version_it = raw.object_v.find("version");
  if (version_it == raw.object_v.end() || version_it->second.kind != JsonValue::Kind::String) {
    throw std::runtime_error("AST JSON missing string field: version");
  }
  ast.version = version_it->second.string_v;
  if (ast.version != gagp::evo::k_ast_prefix_version_current) {
    throw std::runtime_error("AST JSON version must be ast-prefix-v2");
  }
  for (const char* legacy_field : {"linear_rec_binders", "asgp_dc_binders",
                                   "asgp_dp1d_specs", "asgp_dp2d_specs"}) {
    if (raw.object_v.find(legacy_field) != raw.object_v.end()) {
      throw std::runtime_error(std::string("AST legacy sidecar field is not valid in ast-prefix-v2: ") +
                               legacy_field);
    }
  }

  auto nodes_it = raw.object_v.find("nodes");
  if (nodes_it == raw.object_v.end() || nodes_it->second.kind != JsonValue::Kind::Array) {
    throw std::runtime_error("AST JSON missing array field: nodes");
  }
  ast.nodes.reserve(nodes_it->second.array_v.size());
  for (const JsonValue& row : nodes_it->second.array_v) {
    if (row.kind != JsonValue::Kind::Object) {
      throw std::runtime_error("AST node must be an object");
    }
    const int kind = require_int_field_local(row, "kind", "node");
    if (!gagp::evo::is_known_node_kind(kind)) {
      throw std::runtime_error("AST node has an unknown or legacy kind");
    }
    ast.nodes.push_back(gagp::evo::AstNode{
        static_cast<gagp::evo::NodeKind>(kind),
        require_int_field_local(row, "i0", "node"),
        require_int_field_local(row, "i1", "node"),
    });
  }

  auto names_it = raw.object_v.find("names");
  if (names_it == raw.object_v.end() || names_it->second.kind != JsonValue::Kind::Array) {
    throw std::runtime_error("AST JSON missing array field: names");
  }
  ast.names.reserve(names_it->second.array_v.size());
  for (const JsonValue& item : names_it->second.array_v) {
    if (item.kind != JsonValue::Kind::String) {
      throw std::runtime_error("AST names must be strings");
    }
    ast.names.push_back(item.string_v);
  }

  auto consts_it = raw.object_v.find("consts");
  if (consts_it == raw.object_v.end() || consts_it->second.kind != JsonValue::Kind::Array) {
    throw std::runtime_error("AST JSON missing array field: consts");
  }
  ast.consts.reserve(consts_it->second.array_v.size());
  for (const JsonValue& item : consts_it->second.array_v) {
    ast.consts.push_back(gagp::cli_detail::decode_typed_value(item));
  }

  auto regions_it = raw.object_v.find("lexical_regions");
  if (regions_it != raw.object_v.end()) {
    if (regions_it->second.kind != JsonValue::Kind::Array) {
      throw std::runtime_error("AST lexical_regions must be an array");
    }
    ast.lexical_regions.reserve(regions_it->second.array_v.size());
    for (const JsonValue& row : regions_it->second.array_v) {
      if (row.kind != JsonValue::Kind::Object) {
        throw std::runtime_error("AST lexical_regions item must be an object");
      }
      auto bindings_it = row.object_v.find("bindings");
      if (bindings_it == row.object_v.end() || bindings_it->second.kind != JsonValue::Kind::Array) {
        throw std::runtime_error("AST lexical_regions.bindings must be an array");
      }
      gagp::evo::LexicalRegion region;
      region.node_index =
          require_bounded_size_field_local(row, "node_index", "lexical_regions");
      region.body_argument = require_bounded_nonnegative_int_field_local(
          row, "body_argument", "lexical_regions", true);
      region.bindings.reserve(bindings_it->second.array_v.size());
      for (const JsonValue& binding_row : bindings_it->second.array_v) {
        if (binding_row.kind != JsonValue::Kind::Object) {
          throw std::runtime_error("AST lexical_regions.bindings item must be an object");
        }
        auto type_it = binding_row.object_v.find("type");
        if (type_it == binding_row.object_v.end()) {
          throw std::runtime_error("AST lexical_regions.bindings missing field: type");
        }
        region.bindings.push_back(gagp::evo::LexicalBinding{
            require_bounded_nonnegative_int_field_local(
                binding_row, "id", "lexical_regions.bindings", false),
            decode_lexical_binding_type(type_it->second, "lexical_regions.bindings"),
        });
      }
      ast.lexical_regions.push_back(std::move(region));
    }
  }

  auto traversals_it = raw.object_v.find("traversal_specs");
  if (traversals_it != raw.object_v.end()) {
    if (traversals_it->second.kind != JsonValue::Kind::Array) {
      throw std::runtime_error("AST traversal_specs must be an array");
    }
    ast.traversal_specs.reserve(traversals_it->second.array_v.size());
    for (const JsonValue& row : traversals_it->second.array_v) {
      if (row.kind != JsonValue::Kind::Object) {
        throw std::runtime_error("AST traversal_specs item must be an object");
      }
      auto direction_it = row.object_v.find("direction");
      if (direction_it == row.object_v.end() || direction_it->second.kind != JsonValue::Kind::String) {
        throw std::runtime_error("AST traversal_specs.direction must be a string");
      }
      gagp::evo::TraversalDirection direction;
      if (direction_it->second.string_v == "forward") {
        direction = gagp::evo::TraversalDirection::Forward;
      } else if (direction_it->second.string_v == "reverse") {
        direction = gagp::evo::TraversalDirection::Reverse;
      } else {
        throw std::runtime_error("AST traversal_specs.direction must be forward or reverse");
      }
      ast.traversal_specs.push_back(gagp::evo::TraversalSpec{
          require_bounded_size_field_local(row, "node_index", "traversal_specs"),
          direction,
      });
    }
  }

  auto fuel_it = raw.object_v.find("fuel_specs");
  if (fuel_it != raw.object_v.end()) {
    if (fuel_it->second.kind != JsonValue::Kind::Array) {
      throw std::runtime_error("AST fuel_specs must be an array");
    }
    ast.fuel_specs.reserve(fuel_it->second.array_v.size());
    for (const JsonValue& row : fuel_it->second.array_v) {
      if (row.kind != JsonValue::Kind::Object) {
        throw std::runtime_error("AST fuel_specs item must be an object");
      }
      auto charges_it = row.object_v.find("charges");
      if (charges_it == row.object_v.end() || charges_it->second.kind != JsonValue::Kind::Array) {
        throw std::runtime_error("AST fuel_specs.charges must be an array");
      }
      gagp::evo::NodeFuelSpec spec;
      spec.node_index = require_bounded_size_field_local(row, "node_index", "fuel_specs");
      spec.charges.reserve(charges_it->second.array_v.size());
      for (const JsonValue& charge_row : charges_it->second.array_v) {
        if (charge_row.kind != JsonValue::Kind::Object) {
          throw std::runtime_error("AST fuel_specs.charges item must be an object");
        }
        auto event_it = charge_row.object_v.find("event");
        if (event_it == charge_row.object_v.end() ||
            event_it->second.kind != JsonValue::Kind::String) {
          throw std::runtime_error("AST fuel_specs.charges.event must be a string");
        }
        gagp::evo::FuelEvent event;
        if (!gagp::evo::parse_fuel_event(event_it->second.string_v, &event)) {
          throw std::runtime_error("unknown fuel event: " + event_it->second.string_v);
        }
        const int cost = require_bounded_nonnegative_int_field_local(
            charge_row, "cost", "fuel_specs.charges", true);
        spec.charges.push_back(gagp::evo::FuelCharge{
            event,
            static_cast<std::uint32_t>(cost),
        });
      }
      ast.fuel_specs.push_back(std::move(spec));
    }
  }

  auto bounded_it = raw.object_v.find("bounded_region_specs");
  if (bounded_it != raw.object_v.end()) {
    if (bounded_it->second.kind != JsonValue::Kind::Array) {
      throw std::runtime_error("AST bounded_region_specs must be an array");
    }
    if (bounded_it->second.array_v.size() > ast.nodes.size()) {
      throw std::runtime_error("AST bounded_region_specs exceeds node count");
    }
    ast.bounded_region_specs.reserve(bounded_it->second.array_v.size());
    for (const JsonValue& row : bounded_it->second.array_v) {
      if (row.kind != JsonValue::Kind::Object) {
        throw std::runtime_error("AST bounded_region_specs item must be an object");
      }
      reject_unknown_bounded_ast_fields(
          row, {"node_index", "plan", "parameters", "phases"},
          "bounded_region_specs");
      auto plan_it = row.object_v.find("plan");
      auto parameters_it = row.object_v.find("parameters");
      auto phases_it = row.object_v.find("phases");
      if (plan_it == row.object_v.end()) {
        throw std::runtime_error("AST bounded_region_specs missing field: plan");
      }
      if (parameters_it == row.object_v.end() ||
          parameters_it->second.kind != JsonValue::Kind::Array) {
        throw std::runtime_error("AST bounded_region_specs.parameters must be an array");
      }
      if (phases_it == row.object_v.end() ||
          phases_it->second.kind != JsonValue::Kind::Array) {
        throw std::runtime_error("AST bounded_region_specs.phases must be an array");
      }

      gagp::evo::BoundedRegionSpec spec;
      spec.node_index = require_bounded_size_field_local(
          row, "node_index", "bounded_region_specs");
      spec.plan = gagp::cli_detail::decode_region_plan(plan_it->second);
      if (parameters_it->second.array_v.size() != spec.plan.parameter_types.size() ||
          parameters_it->second.array_v.size() > gagp::kRegionParameterCapacity) {
        throw std::runtime_error(
            "AST bounded_region_specs.parameters count does not match plan");
      }
      spec.parameters.reserve(parameters_it->second.array_v.size());
      for (const JsonValue& capture_row : parameters_it->second.array_v) {
        if (capture_row.kind != JsonValue::Kind::Object) {
          throw std::runtime_error(
              "AST bounded_region_specs.parameters item must be an object");
        }
        reject_unknown_bounded_ast_fields(
            capture_row, {"kind", "index"},
            "bounded_region_specs.parameters");
        auto kind_it = capture_row.object_v.find("kind");
        if (kind_it == capture_row.object_v.end()) {
          throw std::runtime_error(
              "AST bounded_region_specs.parameters missing field: kind");
        }
        spec.parameters.push_back(gagp::evo::RegionCapture{
            decode_region_capture_kind(kind_it->second),
            require_bounded_nonnegative_int_field_local(
                capture_row, "index", "bounded_region_specs.parameters", false),
        });
      }

      const std::size_t expected_phases =
          3 + spec.plan.preparations.size() +
          spec.plan.request_expression_types.size() +
          (spec.plan.progress == gagp::RegionProgressKind::Coordinates ? 1 : 0);
      if (phases_it->second.array_v.size() != expected_phases) {
        throw std::runtime_error(
            "AST bounded_region_specs.phases count does not match plan");
      }
      spec.phases.reserve(phases_it->second.array_v.size());
      constexpr std::size_t kMaxRegionAstBindings =
          gagp::kRecurrenceCoordinateCapacity + gagp::kRegionParameterCapacity +
          gagp::kRegionPreparationCapacity + gagp::kRecurrenceRequestCapacity + 1;
      for (const JsonValue& phase_row : phases_it->second.array_v) {
        if (phase_row.kind != JsonValue::Kind::Object) {
          throw std::runtime_error(
              "AST bounded_region_specs.phases item must be an object");
        }
        reject_unknown_bounded_ast_fields(
            phase_row, {"argument", "bindings"},
            "bounded_region_specs.phases");
        auto bindings_it = phase_row.object_v.find("bindings");
        if (bindings_it == phase_row.object_v.end() ||
            bindings_it->second.kind != JsonValue::Kind::Array) {
          throw std::runtime_error(
              "AST bounded_region_specs.phases.bindings must be an array");
        }
        if (bindings_it->second.array_v.size() > kMaxRegionAstBindings) {
          throw std::runtime_error(
              "AST bounded_region_specs.phases.bindings exceeds capacity");
        }
        gagp::evo::RegionAstPhase phase;
        phase.argument = static_cast<std::uint32_t>(
            require_bounded_nonnegative_int_field_local(
                phase_row, "argument", "bounded_region_specs.phases", true));
        phase.bindings.reserve(bindings_it->second.array_v.size());
        for (const JsonValue& binding_row : bindings_it->second.array_v) {
          if (binding_row.kind != JsonValue::Kind::Object) {
            throw std::runtime_error(
                "AST bounded_region_specs.phases.bindings item must be an object");
          }
          reject_unknown_bounded_ast_fields(
              binding_row, {"bank", "slot", "binder_id"},
              "bounded_region_specs.phases.bindings");
          auto bank_it = binding_row.object_v.find("bank");
          if (bank_it == binding_row.object_v.end()) {
            throw std::runtime_error(
                "AST bounded_region_specs.phases.bindings missing field: bank");
          }
          phase.bindings.push_back(gagp::evo::RegionAstBinding{
              gagp::RegionValueSlot{
                  decode_region_slot_bank(bank_it->second),
                  static_cast<std::uint32_t>(
                      require_bounded_nonnegative_int_field_local(
                          binding_row, "slot",
                          "bounded_region_specs.phases.bindings", true)),
              },
              require_bounded_nonnegative_int_field_local(
                  binding_row, "binder_id",
                  "bounded_region_specs.phases.bindings", false),
          });
        }
        spec.phases.push_back(std::move(phase));
      }
      ast.bounded_region_specs.push_back(std::move(spec));
    }
  }
  return ast;
}

std::vector<gagp::evo::EvalCase> parse_cases(const JsonValue& payload) {
  if (payload.kind != JsonValue::Kind::Object) {
    throw std::runtime_error("input JSON must be object");
  }

  auto fv_it = payload.object_v.find("format_version");
  if (fv_it == payload.object_v.end() || fv_it->second.kind != JsonValue::Kind::String) {
    throw std::runtime_error("input JSON must include format_version=fitness-cases");
  }
  if (fv_it->second.string_v != "fitness-cases") {
    throw std::runtime_error("input JSON must include format_version=fitness-cases");
  }

  auto cases_it = payload.object_v.find("cases");
  if (cases_it == payload.object_v.end() || cases_it->second.kind != JsonValue::Kind::Array) {
    throw std::runtime_error("input JSON must include list field: cases");
  }

  std::vector<gagp::evo::EvalCase> out;
  out.reserve(cases_it->second.array_v.size());
  for (const JsonValue& row : cases_it->second.array_v) {
    if (row.kind != JsonValue::Kind::Object) {
      throw std::runtime_error("cases[i] must be object");
    }
    auto inputs_it = row.object_v.find("inputs");
    auto expected_it = row.object_v.find("expected");
    if (inputs_it == row.object_v.end() || expected_it == row.object_v.end()) {
      throw std::runtime_error("cases[i] must include inputs/expected");
    }
    out.push_back(gagp::evo::EvalCase{decode_inputs(inputs_it->second, true),
                                          decode_typed_or_raw_value(expected_it->second, true)});
  }
  if (out.empty()) {
    throw std::runtime_error("cases must not be empty");
  }
  return out;
}

std::string read_text_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("missing input file: " + path);
  }
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

struct LoadedCommandInputs {
  std::vector<gagp::evo::EvalCase> cases;
};

LoadedCommandInputs load_command_inputs(const CliOptions& args) {
  LoadedCommandInputs inputs;
  const std::string cases_text = read_text_file(args.cases_path);
  inputs.cases =
      parse_cases(gagp::cli_detail::JsonParser(cases_text).parse());
  return inputs;
}

std::shared_ptr<const gagp::evo::grammar::CompiledGrammar> load_compiled_definition(
    const std::string& path) {
  const std::string text = read_text_file(path);
  const JsonValue raw = gagp::cli_detail::JsonParser(text, {true, 512}).parse();
  if (raw.kind == JsonValue::Kind::Object) {
    const auto version = raw.object_v.find("format_version");
    if (version != raw.object_v.end() && version->second.kind == JsonValue::Kind::String &&
        version->second.string_v == "grammar-config") {
      throw std::invalid_argument(
          "--grammar-definition received legacy format_version=grammar-config; "
          "migrate the config offline to grammar-definition-v2 before evolution");
    }
  }
  return std::make_shared<const gagp::evo::grammar::CompiledGrammar>(
      gagp::evo::grammar::compile_grammar(gagp::evo::grammar::load_definition(path)));
}

int compiled_limit(std::uint32_t value, const char* field) {
  if (value > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
    throw std::invalid_argument(std::string("grammar definition ") + field +
                                " exceeds the native evolution limit");
  return static_cast<int>(value);
}

void apply_compiled_contract(const CliOptions& args,
    const std::shared_ptr<const gagp::evo::grammar::CompiledGrammar>& grammar,
    gagp::evo::EvolutionConfig* cfg) {
  const int max_depth = compiled_limit(grammar->search_limits().max_depth,
                                       "search_limits.max_depth");
  const int max_nodes = compiled_limit(grammar->search_limits().max_nodes,
                                       "search_limits.max_nodes");
  const int fuel = compiled_limit(grammar->execution_limits().fuel,
                                  "execution_limits.fuel");
  if (args.fuel_explicit && args.fuel != fuel)
    throw std::invalid_argument("--fuel conflicts with grammar definition execution_limits.fuel=" +
                                std::to_string(fuel));
  if (args.max_expr_depth_explicit && args.max_expr_depth != max_depth)
    throw std::invalid_argument("--max-expr-depth conflicts with grammar definition search_limits.max_depth=" +
                                std::to_string(max_depth));
  if (args.max_total_nodes_explicit && args.max_total_nodes != max_nodes)
    throw std::invalid_argument("--max-total-nodes conflicts with grammar definition search_limits.max_nodes=" +
                                std::to_string(max_nodes));
  if (args.max_stmts_per_block_explicit)
    throw std::invalid_argument(
        "--max-stmts-per-block cannot override a compiled grammar; encode the block bound in --grammar-definition");
  if (args.max_for_k_explicit)
    throw std::invalid_argument(
        "--max-for-k cannot override a compiled grammar; encode loop-bound constants in --grammar-definition");
  if (args.max_call_args_explicit)
    throw std::invalid_argument(
        "--max-call-args cannot override a compiled grammar; encode primitive arity in --grammar-definition");

  cfg->compiled_grammar = grammar;
  cfg->generation_request = gagp::evo::grammar::entry_request(*grammar);
  cfg->fuel = fuel;
}

gagp::evo::EvolutionConfig make_evolution_config(const CliOptions& args) {
  gagp::evo::EvolutionConfig cfg;
  cfg.population_size = args.population_size;
  cfg.generations = args.generations;
  cfg.mutation_rate = args.mutation_rate;
  cfg.mutation_subtree_prob = args.mutation_subtree_prob;
  cfg.penalty = args.penalty;
  cfg.eval_engine =
      (args.engine == "gpu") ? gagp::evo::EvalEngine::GPU : gagp::evo::EvalEngine::CPU;
  cfg.reproduction_backend =
      gagp::evo::repro::parse_reproduction_backend_name(args.repro_backend);
  cfg.cpu_repro_ablation =
      gagp::evo::repro::parse_cpu_repro_ablation_name(args.cpu_repro_ablation);
  cfg.repro_overlap = args.repro_overlap;
  cfg.gpu_blocksize = args.blocksize;
  cfg.selection_pressure = args.selection_pressure;
  cfg.seed = args.seed;
  cfg.fuel = args.fuel;
  cfg.skip_final_eval = args.skip_final_eval;
  cfg.retain_final_population = args.retain_final_population;
  return cfg;
}

}  // namespace

gagp::evo::AstProgram gagp::cli_detail::decode_ast_json(const JsonValue& raw) {
  return decode_ast_json_impl(raw);
}

std::vector<gagp::evo::EvalCase> gagp::cli_detail::decode_fitness_cases_json(const JsonValue& raw) {
  return parse_cases(raw);
}

std::string gagp::cli_detail::encode_ast_json(const evo::AstProgram& ast) {
  std::ostringstream out;
  write_ast_json(out, ast);
  return out.str();
}

int gagp::cli_detail::run_eval_ast_command(const CliOptions& args) {
      const LoadedCommandInputs inputs = load_command_inputs(args);
      gagp::evo::EvolutionConfig cfg = make_evolution_config(args);
      if (cfg.eval_engine != gagp::evo::EvalEngine::CPU) {
        throw std::runtime_error("--eval-ast-json currently supports --engine cpu only");
      }
      const auto ast_text = read_text_file(args.eval_ast_json);
      if (ast_text.size() > 256u * 1024u * 1024u) {
        throw std::invalid_argument("AST JSON exceeds 256 MiB");
      }
      const JsonValue ast_payload =
          gagp::cli_detail::JsonParser(ast_text, {false, 512}).parse();
      gagp::evo::ProgramGenome genome;
      const auto format = ast_payload.object_v.find("format_version");
      if (format != ast_payload.object_v.end() && format->second.kind == JsonValue::Kind::String &&
          (format->second.string_v == kGeneratedGrammarArtifactVersion ||
           format->second.string_v == kMaterializedGrammarArtifactVersion)) {
        auto materialized = format->second.string_v == kGeneratedGrammarArtifactVersion
            ? decode_materialized_program(ast_text)
            : decode_migrated_materialized_program(ast_text);
        const auto fixture = gagp::evo::prepare_case_set(inputs.cases);
        if (fixture.input_specs.size() != materialized.inputs.size() ||
            fixture.expected_return_type != materialized.return_type)
          throw std::invalid_argument("grammar artifact fixture schema differs from --cases");
        for (const auto& input : materialized.inputs) {
          const auto found = std::find_if(fixture.input_specs.begin(), fixture.input_specs.end(),
              [&](const auto& candidate) { return candidate.name == input.name && candidate.type == input.type; });
          if (found == fixture.input_specs.end())
            throw std::invalid_argument("grammar artifact input schema differs from --cases");
        }
        if (args.fuel_explicit && args.fuel != static_cast<int>(materialized.execution_limits.fuel))
          throw std::invalid_argument("--fuel differs from grammar artifact execution contract; use its recorded fuel");
        cfg.fuel = static_cast<int>(materialized.execution_limits.fuel);
        genome = std::move(materialized.genome);
      } else {
        genome.ast = decode_ast_json(ast_payload);
      }
      const gagp::evo::AstVerifyResult verified = gagp::evo::verify_ast(
          genome.ast, gagp::evo::canonical_input_specs(inputs.cases));
      if (!verified) {
        throw std::runtime_error(
            std::string("invalid AST (") +
            gagp::evo::verify_code_name(verified.diagnostic.code) + ") at " +
            verified.diagnostic.path + ": " + verified.diagnostic.message);
      }
      genome.meta = gagp::evo::build_genome_meta(genome.ast);
      const std::vector<gagp::evo::ScoredGenome> scored =
          gagp::evo::evaluate_population({genome}, inputs.cases, cfg);
      if (scored.empty()) {
        throw std::runtime_error("AST evaluation produced no score");
      }
      const double fitness = canonicalize_metric(scored[0].fitness);
      std::cout << "AST_EVAL fitness=" << std::fixed << std::setprecision(6)
                << fitness << " program_key=" << scored[0].genome.meta.program_key << "\n";
      if (!args.out_json.empty()) {
        std::ofstream out(args.out_json);
        if (!out) {
          throw std::runtime_error("failed to open out-json path");
        }
        out << "{\n";
        out << "  \"format_version\": \"ast-eval-result\",\n";
        out << "  \"meta\": {\n";
        out << "    \"cases_path\": \"" << json_escape(args.cases_path) << "\",\n";
        out << "    \"ast_json\": \"" << json_escape(args.eval_ast_json) << "\",\n";
        out << "    \"eval_engine\": \"" << gagp::evo::eval_engine_name(cfg.eval_engine) << "\",\n";
        out << "    \"fuel\": " << cfg.fuel << ",\n";
        out << "    \"penalty\": " << std::setprecision(17) << cfg.penalty << "\n";
        out << "  },\n";
        out << "  \"result\": {\n";
        out << "    \"fitness\": " << std::setprecision(17) << scored[0].fitness << ",\n";
        out << "    \"program_key\": \"" << json_escape(scored[0].genome.meta.program_key) << "\"\n";
        out << "  }\n";
        out << "}\n";
      }
      return 0;
}

int gagp::cli_detail::run_evolve_command(const CliOptions& args) {
    const LoadedCommandInputs inputs = load_command_inputs(args);
    gagp::evo::EvolutionConfig cfg = make_evolution_config(args);
    const std::vector<gagp::evo::EvalCase>& cases = inputs.cases;
    const auto compiled_grammar = load_compiled_definition(args.grammar_definition_path);
    apply_compiled_contract(args, compiled_grammar, &cfg);

    std::vector<gagp::evo::ProgramGenome> initial_population;
    const std::vector<gagp::evo::ProgramGenome>* initial_population_ptr = nullptr;
    std::string population_source = "generated";
    if (!args.population_json.empty()) {
      const std::string population_text = read_text_file(args.population_json);
      const JsonValue population_payload =
          gagp::cli_detail::JsonParser(population_text, {true, 512}).parse();
      const auto format = population_payload.kind == JsonValue::Kind::Object
          ? population_payload.object_v.find("format_version")
          : population_payload.object_v.end();
      if (population_payload.kind == JsonValue::Kind::Object &&
          format != population_payload.object_v.end() &&
          format->second.kind == JsonValue::Kind::String &&
          format->second.string_v == "population-seeds") {
        throw std::invalid_argument(
            "legacy population-seeds cannot replay compiled grammar evolution exactly; "
            "materialize the population with the frozen legacy build, then migrate it");
      }
      initial_population = replay_generated_population_artifact(
          population_text, compiled_grammar.get());
      cfg.population_size = static_cast<int>(initial_population.size());
      initial_population_ptr = &initial_population;
      population_source = "population_json";
    }

    const gagp::evo::EvolutionResult result =
        gagp::evo::evolve_population(cases, cfg, initial_population_ptr);
    const char* selection_label = "round_based_tournament";
    const char* crossover_label = "typed_subtree";

    struct HistoryRow {
      int generation = 0;
      double best_fitness = 0.0;
      double mean_fitness = 0.0;
      std::string program_key;
    };
    std::vector<HistoryRow> history_rows;
    history_rows.reserve(result.history_best.size());

    for (int i = 0; i < static_cast<int>(result.history_best.size()); ++i) {
      const auto& best = result.history_best[static_cast<std::size_t>(i)];
      const double best_fit = canonicalize_metric(result.history_best_fitness[static_cast<std::size_t>(i)]);
      const double mean_fit = canonicalize_metric(result.history_mean_fitness[static_cast<std::size_t>(i)]);
      const std::string& program_key = best.genome.meta.program_key;
      history_rows.push_back(HistoryRow{i, best_fit, mean_fit, program_key});

      std::cout << "GEN " << std::setfill('0') << std::setw(3) << i << std::setfill(' ') << " best="
                << std::fixed << std::setprecision(6) << best_fit << " mean=" << std::fixed
                << std::setprecision(6) << mean_fit << " program_key=" << program_key << "\n";

      if (args.show_program == "ast" || args.show_program == "both") {
        std::cout << "AST " << std::setfill('0') << std::setw(3) << i << std::setfill(' ') << ": "
                  << gagp::evo::ast_to_string(best.genome.ast) << "\n";
      }
      if (args.show_program == "bytecode" || args.show_program == "both") {
        const gagp::BytecodeProgram bc = gagp::evo::compile_for_eval(best.genome);
        std::cout << "BYTECODE " << std::setfill('0') << std::setw(3) << i << std::setfill(' ')
                  << ": n_locals=" << bc.n_locals << " consts=" << bc.consts.size() << " code="
                  << bc.code.size() << "\n";

        std::cout << "BYTECODE_HEAD";
        const std::size_t cap = std::min<std::size_t>(12, bc.code.size());
        for (std::size_t j = 0; j < cap; ++j) {
          std::cout << " " << j << ":" << gagp::opcode_name(bc.code[j].op);
        }
        std::cout << "\n";
      }
    }

    if (result.final_eval_skipped) {
      const HistoryRow& last = history_rows.back();
      std::cout << "FINAL skipped=true last_history_best=" << std::fixed << std::setprecision(6)
                << canonicalize_metric(last.best_fitness)
                << " program_key=" << last.program_key
                << " repro_backend=" << gagp::evo::repro::reproduction_backend_name(cfg.reproduction_backend)
                << " cpu_repro_ablation=" << gagp::evo::repro::cpu_repro_ablation_name(cfg.cpu_repro_ablation)
                << " repro_overlap=" << (cfg.repro_overlap ? "on" : "off")
                << " selection=" << selection_label
                << " crossover=" << crossover_label << "\n";
    } else {
      std::cout << "FINAL best=" << std::fixed << std::setprecision(6) << canonicalize_metric(result.best.fitness)
                << " program_key=" << result.best.genome.meta.program_key
                << " repro_backend=" << gagp::evo::repro::reproduction_backend_name(cfg.reproduction_backend)
                << " cpu_repro_ablation=" << gagp::evo::repro::cpu_repro_ablation_name(cfg.cpu_repro_ablation)
                << " repro_overlap=" << (cfg.repro_overlap ? "on" : "off")
                << " selection=" << selection_label
                << " crossover=" << crossover_label << "\n";
    }

    if (args.timing == "summary" || args.timing == "all") {
      double gen_eval_sum = 0.0;
      double gen_repro_sum = 0.0;
      for (const auto& generation : result.timing.generations) {
        gen_eval_sum += generation.eval_ms;
        gen_repro_sum += generation.repro_ms;
      }
      const auto& eval = result.timing.evaluation_totals;
      const auto& repro = result.timing.reproduction_totals;
      std::cout << "TIMING phase=init_population ms=" << std::fixed << std::setprecision(3)
                << result.timing.init_population_ms << "\n";
      std::cout << "TIMING phase=generations_eval_total ms=" << std::fixed << std::setprecision(3)
                << gen_eval_sum << "\n";
      std::cout << "TIMING phase=generations_repro_total ms=" << std::fixed << std::setprecision(3)
                << gen_repro_sum << "\n";
      std::cout << "TIMING phase=generations_selection_total ms=" << std::fixed << std::setprecision(3)
                << repro.selection_ms << "\n";
      std::cout << "TIMING phase=generations_crossover_total ms=" << std::fixed << std::setprecision(3)
                << repro.crossover_ms << "\n";
      std::cout << "TIMING phase=generations_mutation_total ms=" << std::fixed << std::setprecision(3)
                << repro.mutation_ms << "\n";
      std::cout << "TIMING phase=generations_repro_prepare_inputs_total ms=" << std::fixed
                << std::setprecision(3) << repro.prepare_inputs_ms << "\n";
      std::cout << "TIMING phase=generations_repro_setup_total ms=" << std::fixed << std::setprecision(3)
                << repro.setup_ms << "\n";
      std::cout << "TIMING phase=generations_repro_preprocess_total ms=" << std::fixed
                << std::setprecision(3) << repro.preprocess_ms << "\n";
      std::cout << "TIMING phase=generations_repro_pack_total ms=" << std::fixed << std::setprecision(3)
                << repro.pack_ms << "\n";
      std::cout << "TIMING phase=generations_repro_upload_total ms=" << std::fixed << std::setprecision(3)
                << repro.upload_ms << "\n";
      std::cout << "TIMING phase=generations_repro_kernel_total ms=" << std::fixed << std::setprecision(3)
                << repro.kernel_ms << "\n";
      std::cout << "TIMING phase=generations_repro_copyback_total ms=" << std::fixed
                << std::setprecision(3) << repro.copyback_ms << "\n";
      std::cout << "TIMING phase=generations_repro_decode_total ms=" << std::fixed
                << std::setprecision(3) << repro.decode_ms << "\n";
      std::cout << "TIMING phase=generations_repro_teardown_total ms=" << std::fixed
                << std::setprecision(3) << repro.teardown_ms << "\n";
      std::cout << "TIMING phase=generations_repro_selection_kernel_total ms=" << std::fixed
                << std::setprecision(3) << repro.selection_kernel_ms << "\n";
      std::cout << "TIMING phase=generations_repro_variation_kernel_total ms=" << std::fixed
                << std::setprecision(3) << repro.variation_kernel_ms << "\n";
      std::cout << "TIMING phase=generations_repro_crossover_attempts_total count="
                << repro.variation.crossover_attempts << "\n";
      std::cout << "TIMING phase=generations_repro_mutation_attempts_total count="
                << repro.variation.mutation_attempts << "\n";
      std::cout << "TIMING phase=generations_repro_contract_rejections_total count="
                << repro.variation.contract_rejections << "\n";
      std::cout << "TIMING phase=generations_repro_budget_rejections_total count="
                << repro.variation.budget_rejections << "\n";
      std::cout << "TIMING phase=generations_repro_generation_rejections_total count="
                << repro.variation.generation_rejections << "\n";
      std::cout << "TIMING phase=generations_repro_acceptance_rejections_total count="
                << repro.variation.acceptance_rejections << "\n";
      std::cout << "TIMING phase=generations_repro_fallback_children_total count="
                << repro.variation.fallback_children << "\n";
      std::cout << "TIMING phase=generations_repro_unchanged_children_total count="
                << repro.variation.unchanged_children << "\n";
      std::cout << "TIMING phase=generations_repro_changed_children_total count="
                << repro.variation.changed_children << "\n";
      std::cout << "TIMING phase=cpu_compile_total ms=" << std::fixed << std::setprecision(3)
                << eval.cpu_compile_ms << "\n";
      std::cout << "TIMING phase=final_eval ms=" << std::fixed << std::setprecision(3)
                << result.timing.final_eval_ms << "\n";
      if (cfg.eval_engine == gagp::evo::EvalEngine::GPU) {
        std::cout << "TIMING phase=gpu_eval_init ms=" << std::fixed << std::setprecision(3)
                  << result.timing.gpu_eval_init_ms << "\n";
        std::cout << "TIMING phase=gpu_compile_total ms=" << std::fixed << std::setprecision(3)
                  << eval.gpu_compile_ms << "\n";
        std::cout << "TIMING phase=gpu_eval_call_total ms=" << std::fixed << std::setprecision(3)
                  << eval.gpu_eval_call_ms << "\n";
        std::cout << "TIMING phase=gpu_eval_pack_total ms=" << std::fixed << std::setprecision(3)
                  << eval.gpu_eval_pack_ms << "\n";
        std::cout << "TIMING phase=gpu_eval_launch_prep_total ms=" << std::fixed << std::setprecision(3)
                  << eval.gpu_eval_launch_prep_ms << "\n";
        std::cout << "TIMING phase=gpu_eval_upload_total ms=" << std::fixed << std::setprecision(3)
                  << eval.gpu_eval_upload_ms << "\n";
        std::cout << "TIMING phase=gpu_eval_pack_upload_total ms=" << std::fixed << std::setprecision(3)
                  << eval.gpu_eval_pack_upload_ms() << "\n";
        std::cout << "TIMING phase=gpu_eval_kernel_total ms=" << std::fixed << std::setprecision(3)
                  << eval.gpu_eval_kernel_ms << "\n";
        std::cout << "TIMING phase=gpu_eval_copyback_total ms=" << std::fixed << std::setprecision(3)
                  << eval.gpu_eval_copyback_ms << "\n";
        std::cout << "TIMING phase=gpu_eval_teardown_total ms=" << std::fixed << std::setprecision(3)
                  << eval.gpu_eval_teardown_ms << "\n";
      }
      std::cout << "TIMING phase=total ms=" << std::fixed << std::setprecision(3)
                << result.timing.total_ms << "\n";
    }

    if (args.timing == "per_gen" || args.timing == "all") {
      for (std::size_t i = 0; i < result.timing.generations.size(); ++i) {
        const auto& generation = result.timing.generations[i];
        const auto& eval = generation.evaluation;
        const auto& repro = generation.reproduction;
        std::cout << "TIMING gen=" << std::setfill('0') << std::setw(3) << i << std::setfill(' ')
                  << " eval_ms=" << std::fixed << std::setprecision(3) << generation.eval_ms
                  << " repro_ms=" << generation.repro_ms
                  << " total_ms=" << generation.total_ms
                  << " selection_ms=" << repro.selection_ms
                  << " crossover_ms=" << repro.crossover_ms
                  << " mutation_ms=" << repro.mutation_ms
                  << " repro_prepare_inputs_ms=" << repro.prepare_inputs_ms
                  << " repro_setup_ms=" << repro.setup_ms
                  << " repro_preprocess_ms=" << repro.preprocess_ms
                  << " repro_pack_ms=" << repro.pack_ms
                  << " repro_upload_ms=" << repro.upload_ms
                  << " repro_kernel_ms=" << repro.kernel_ms
                  << " repro_copyback_ms=" << repro.copyback_ms
                  << " repro_decode_ms=" << repro.decode_ms
                  << " repro_teardown_ms=" << repro.teardown_ms
                  << " repro_selection_kernel_ms=" << repro.selection_kernel_ms
                  << " repro_variation_kernel_ms=" << repro.variation_kernel_ms
                  << " generation_repro_crossover_attempts=" << repro.variation.crossover_attempts
                  << " generation_repro_mutation_attempts=" << repro.variation.mutation_attempts
                  << " generation_repro_contract_rejections=" << repro.variation.contract_rejections
                  << " generation_repro_budget_rejections=" << repro.variation.budget_rejections
                  << " generation_repro_generation_rejections=" << repro.variation.generation_rejections
                  << " generation_repro_acceptance_rejections=" << repro.variation.acceptance_rejections
                  << " generation_repro_fallback_children=" << repro.variation.fallback_children
                  << " generation_repro_unchanged_children=" << repro.variation.unchanged_children
                  << " generation_repro_changed_children=" << repro.variation.changed_children
                  << " cpu_compile_ms=" << eval.cpu_compile_ms << "\n";
        if (cfg.eval_engine == gagp::evo::EvalEngine::GPU) {
          std::cout << "TIMING gpu_gen=" << std::setfill('0') << std::setw(3) << i << std::setfill(' ')
                    << " gpu_compile_ms=" << std::fixed << std::setprecision(3)
                    << eval.gpu_compile_ms
                    << " gpu_eval_call_ms=" << eval.gpu_eval_call_ms
                    << " gpu_eval_pack_ms=" << eval.gpu_eval_pack_ms
                    << " gpu_eval_launch_prep_ms=" << eval.gpu_eval_launch_prep_ms
                    << " gpu_eval_upload_ms=" << eval.gpu_eval_upload_ms
                    << " gpu_eval_pack_upload_ms=" << eval.gpu_eval_pack_upload_ms()
                    << " gpu_eval_kernel_ms=" << eval.gpu_eval_kernel_ms
                    << " gpu_eval_copyback_ms=" << eval.gpu_eval_copyback_ms
                    << " gpu_eval_teardown_ms=" << eval.gpu_eval_teardown_ms << "\n";
        }
      }
    }

    if (!args.out_json.empty()) {
      std::ofstream out(args.out_json);
      if (!out) {
        throw std::runtime_error("failed to open out-json path");
      }
      const auto& timing = result.timing;
      const auto& eval = timing.evaluation_totals;
      const auto& repro = timing.reproduction_totals;

      out << "{\n";
      out << "  \"meta\": {\n";
      out << "    \"cases_path\": \"" << json_escape(args.cases_path) << "\",\n";
      out << "    \"population_size\": " << cfg.population_size << ",\n";
      out << "    \"generations\": " << cfg.generations << ",\n";
      out << "    \"population_source\": \"" << population_source << "\",\n";
      if (!args.population_json.empty()) {
        out << "    \"population_json\": \"" << json_escape(args.population_json) << "\",\n";
      } else {
        out << "    \"population_json\": null,\n";
      }
      out << "    \"grammar_definition\": {\n";
      out << "      \"path\": \"" << json_escape(args.grammar_definition_path) << "\",\n";
      out << "      \"hash\": \"" << json_escape(compiled_grammar->content_hash()) << "\"\n";
      out << "    },\n";
      out << "    \"selection\": \"" << selection_label << "\",\n";
      out << "    \"crossover_method\": \"" << crossover_label << "\",\n";
      out << "    \"eval_engine\": \"" << gagp::evo::eval_engine_name(cfg.eval_engine) << "\",\n";
      out << "    \"reproduction_backend\": \""
          << gagp::evo::repro::reproduction_backend_name(cfg.reproduction_backend) << "\",\n";
      out << "    \"cpu_repro_ablation\": \""
          << gagp::evo::repro::cpu_repro_ablation_name(cfg.cpu_repro_ablation) << "\",\n";
      out << "    \"repro_overlap\": " << (cfg.repro_overlap ? "true" : "false") << ",\n";
      out << "    \"skip_final_eval\": " << (cfg.skip_final_eval ? "true" : "false") << ",\n";
      out << "    \"retain_final_population\": " << (cfg.retain_final_population ? "true" : "false") << ",\n";
      out << "    \"gpu_blocksize\": " << cfg.gpu_blocksize << ",\n";
      out << "    \"seed\": " << cfg.seed << ",\n";
      out << "    \"timing\": {\n";
      out << "      \"init_population_ms\": " << std::setprecision(17) << timing.init_population_ms << ",\n";
      out << "      \"gpu_eval_init_ms\": " << timing.gpu_eval_init_ms << ",\n";
      out << "      \"final_eval_ms\": " << timing.final_eval_ms << ",\n";
      out << "      \"cpu_compile_ms_total\": " << eval.cpu_compile_ms << ",\n";
      out << "      \"gpu_compile_ms_total\": " << eval.gpu_compile_ms << ",\n";
      out << "      \"gpu_eval_call_ms_total\": " << eval.gpu_eval_call_ms << ",\n";
      out << "      \"gpu_eval_pack_ms_total\": " << eval.gpu_eval_pack_ms << ",\n";
      out << "      \"gpu_eval_launch_prep_ms_total\": " << eval.gpu_eval_launch_prep_ms << ",\n";
      out << "      \"gpu_eval_upload_ms_total\": " << eval.gpu_eval_upload_ms << ",\n";
      out << "      \"gpu_eval_pack_upload_ms_total\": " << eval.gpu_eval_pack_upload_ms() << ",\n";
      out << "      \"gpu_eval_kernel_ms_total\": " << eval.gpu_eval_kernel_ms << ",\n";
      out << "      \"gpu_eval_copyback_ms_total\": " << eval.gpu_eval_copyback_ms << ",\n";
      out << "      \"gpu_eval_teardown_ms_total\": " << eval.gpu_eval_teardown_ms << ",\n";
      out << "      \"generations_selection_ms_total\": " << repro.selection_ms << ",\n";
      out << "      \"generations_crossover_ms_total\": " << repro.crossover_ms << ",\n";
      out << "      \"generations_mutation_ms_total\": " << repro.mutation_ms << ",\n";
      out << "      \"generations_repro_prepare_inputs_ms_total\": "
          << repro.prepare_inputs_ms << ",\n";
      out << "      \"generations_repro_setup_ms_total\": " << repro.setup_ms << ",\n";
      out << "      \"generations_repro_preprocess_ms_total\": " << repro.preprocess_ms
          << ",\n";
      out << "      \"generations_repro_pack_ms_total\": " << repro.pack_ms << ",\n";
      out << "      \"generations_repro_upload_ms_total\": " << repro.upload_ms << ",\n";
      out << "      \"generations_repro_kernel_ms_total\": " << repro.kernel_ms << ",\n";
      out << "      \"generations_repro_copyback_ms_total\": " << repro.copyback_ms
          << ",\n";
      out << "      \"generations_repro_decode_ms_total\": " << repro.decode_ms
          << ",\n";
      out << "      \"generations_repro_teardown_ms_total\": " << repro.teardown_ms
          << ",\n";
      out << "      \"generations_repro_selection_kernel_ms_total\": "
          << repro.selection_kernel_ms << ",\n";
      out << "      \"generations_repro_variation_kernel_ms_total\": "
          << repro.variation_kernel_ms << ",\n";
      out << "      \"generations_repro_crossover_attempts_total\": "
          << repro.variation.crossover_attempts << ",\n";
      out << "      \"generations_repro_mutation_attempts_total\": "
          << repro.variation.mutation_attempts << ",\n";
      out << "      \"generations_repro_contract_rejections_total\": "
          << repro.variation.contract_rejections << ",\n";
      out << "      \"generations_repro_budget_rejections_total\": "
          << repro.variation.budget_rejections << ",\n";
      out << "      \"generations_repro_generation_rejections_total\": "
          << repro.variation.generation_rejections << ",\n";
      out << "      \"generations_repro_acceptance_rejections_total\": "
          << repro.variation.acceptance_rejections << ",\n";
      out << "      \"generations_repro_fallback_children_total\": "
          << repro.variation.fallback_children << ",\n";
      out << "      \"generations_repro_unchanged_children_total\": "
          << repro.variation.unchanged_children << ",\n";
      out << "      \"generations_repro_changed_children_total\": "
          << repro.variation.changed_children << ",\n";
      out << "      \"total_ms\": " << timing.total_ms << "\n";
      out << "    }\n";
      out << "  },\n";

      out << "  \"history\": [\n";
      for (std::size_t i = 0; i < history_rows.size(); ++i) {
        const auto& row = history_rows[i];
        out << "    {\"generation\": " << row.generation << ", \"best_fitness\": "
            << std::setprecision(17) << row.best_fitness << ", \"mean_fitness\": " << row.mean_fitness
            << ", \"program_key\": \"" << json_escape(row.program_key) << "\"}";
        if (i + 1 < history_rows.size()) {
          out << ",";
        }
        out << "\n";
      }
      out << "  ],\n";

      auto dump_series = [&](const char* name, auto value_at, bool last) {
        out << "    \"" << name << "\": [";
        for (std::size_t i = 0; i < timing.generations.size(); ++i) {
          if (i > 0) out << ", ";
          out << std::setprecision(17) << value_at(timing.generations[i]);
        }
        out << "]";
        if (!last) out << ",";
        out << "\n";
      };

      out << "  \"timing\": {\n";
      dump_series("generation_eval_ms", [](const auto& g) { return g.eval_ms; }, false);
      dump_series("generation_repro_ms", [](const auto& g) { return g.repro_ms; }, false);
      dump_series("generation_cpu_compile_ms", [](const auto& g) { return g.evaluation.cpu_compile_ms; }, false);
      dump_series("generation_gpu_compile_ms", [](const auto& g) { return g.evaluation.gpu_compile_ms; }, false);
      dump_series("generation_gpu_eval_call_ms", [](const auto& g) { return g.evaluation.gpu_eval_call_ms; }, false);
      dump_series("generation_gpu_eval_pack_ms", [](const auto& g) { return g.evaluation.gpu_eval_pack_ms; }, false);
      dump_series("generation_gpu_eval_launch_prep_ms", [](const auto& g) { return g.evaluation.gpu_eval_launch_prep_ms; }, false);
      dump_series("generation_gpu_eval_upload_ms", [](const auto& g) { return g.evaluation.gpu_eval_upload_ms; }, false);
      dump_series("generation_gpu_eval_pack_upload_ms", [](const auto& g) { return g.evaluation.gpu_eval_pack_upload_ms(); }, false);
      dump_series("generation_gpu_eval_kernel_ms", [](const auto& g) { return g.evaluation.gpu_eval_kernel_ms; }, false);
      dump_series("generation_gpu_eval_copyback_ms", [](const auto& g) { return g.evaluation.gpu_eval_copyback_ms; }, false);
      dump_series("generation_gpu_eval_teardown_ms", [](const auto& g) { return g.evaluation.gpu_eval_teardown_ms; }, false);
      dump_series("generation_selection_ms", [](const auto& g) { return g.reproduction.selection_ms; }, false);
      dump_series("generation_crossover_ms", [](const auto& g) { return g.reproduction.crossover_ms; }, false);
      dump_series("generation_mutation_ms", [](const auto& g) { return g.reproduction.mutation_ms; }, false);
      dump_series("generation_repro_prepare_inputs_ms", [](const auto& g) { return g.reproduction.prepare_inputs_ms; }, false);
      dump_series("generation_repro_setup_ms", [](const auto& g) { return g.reproduction.setup_ms; }, false);
      dump_series("generation_repro_preprocess_ms", [](const auto& g) { return g.reproduction.preprocess_ms; }, false);
      dump_series("generation_repro_pack_ms", [](const auto& g) { return g.reproduction.pack_ms; }, false);
      dump_series("generation_repro_upload_ms", [](const auto& g) { return g.reproduction.upload_ms; }, false);
      dump_series("generation_repro_kernel_ms", [](const auto& g) { return g.reproduction.kernel_ms; }, false);
      dump_series("generation_repro_copyback_ms", [](const auto& g) { return g.reproduction.copyback_ms; }, false);
      dump_series("generation_repro_decode_ms", [](const auto& g) { return g.reproduction.decode_ms; }, false);
      dump_series("generation_repro_teardown_ms", [](const auto& g) { return g.reproduction.teardown_ms; }, false);
      dump_series("generation_repro_selection_kernel_ms", [](const auto& g) { return g.reproduction.selection_kernel_ms; }, false);
      dump_series("generation_repro_variation_kernel_ms", [](const auto& g) { return g.reproduction.variation_kernel_ms; }, false);
      dump_series("generation_repro_crossover_attempts", [](const auto& g) { return g.reproduction.variation.crossover_attempts; }, false);
      dump_series("generation_repro_mutation_attempts", [](const auto& g) { return g.reproduction.variation.mutation_attempts; }, false);
      dump_series("generation_repro_contract_rejections", [](const auto& g) { return g.reproduction.variation.contract_rejections; }, false);
      dump_series("generation_repro_budget_rejections", [](const auto& g) { return g.reproduction.variation.budget_rejections; }, false);
      dump_series("generation_repro_generation_rejections", [](const auto& g) { return g.reproduction.variation.generation_rejections; }, false);
      dump_series("generation_repro_acceptance_rejections", [](const auto& g) { return g.reproduction.variation.acceptance_rejections; }, false);
      dump_series("generation_repro_fallback_children", [](const auto& g) { return g.reproduction.variation.fallback_children; }, false);
      dump_series("generation_repro_unchanged_children", [](const auto& g) { return g.reproduction.variation.unchanged_children; }, false);
      dump_series("generation_repro_changed_children", [](const auto& g) { return g.reproduction.variation.changed_children; }, false);
      dump_series("generation_total_ms", [](const auto& g) { return g.total_ms; }, true);
      out << "  },\n";

      out << "  \"final\": {\n";
      out << "    \"skipped\": " << (result.final_eval_skipped ? "true" : "false");
      if (!result.final_eval_skipped) {
        const gagp::evo::ProgramGenome best_output =
            gagp::evo::repro::compact_genome_tables(result.best.genome);
        out << ",\n";
        out << "    \"best_fitness\": " << std::setprecision(17) << result.best.fitness << ",\n";
        out << "    \"program_key\": \"" << json_escape(best_output.meta.program_key) << "\",\n";
        out << "    \"ast_repr\": \"" << json_escape(gagp::evo::ast_to_string(best_output.ast)) << "\",\n";
        out << "    \"ast_names\": [";
        for (std::size_t i = 0; i < best_output.ast.names.size(); ++i) {
          if (i > 0) out << ", ";
          out << "\"" << json_escape(best_output.ast.names[i]) << "\"";
        }
        out << "],\n";
        out << "    \"ast_consts\": [";
        for (std::size_t i = 0; i < best_output.ast.consts.size(); ++i) {
          if (i > 0) out << ", ";
          write_value_json(out, best_output.ast.consts[i]);
        }
        out << "],\n";
        out << "    \"ast\": ";
        write_ast_json(out, best_output.ast);
        out << "\n";
      } else {
        out << "\n";
      }
      out << "  }\n";
      out << "}\n";
    }

    return 0;
}
