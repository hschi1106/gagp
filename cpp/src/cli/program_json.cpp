// JSON codecs for programs (ast-prefix-v2) and fitness cases (fitness-cases).
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
#include "gagp/evolution/grammar/catalog.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/repro/pack.hpp"
#include "gagp/cli/codec.hpp"
#include "gagp/cli/program_json.hpp"
#include "gagp/cli/search_config.hpp"
#include "gagp/cli/json.hpp"
#include "gagp/cli/region_codec.hpp"
#include "gagp/cli/grammar_artifact.hpp"
#include "gagp/cli/grammar_population_artifact.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace {

using gagp::Value;
using gagp::ValueTag;
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

