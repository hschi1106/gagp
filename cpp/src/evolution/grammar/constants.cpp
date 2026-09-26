#include "gagp/evolution/grammar/constants.hpp"
#include "gagp/evolution/grammar/catalog.hpp"
#include "gagp/evolution/grammar/numeric_sampling.hpp"

#include <charconv>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace gagp::evo::grammar {
namespace {
using Json = cli_detail::JsonValue;
using Kind = Json::Kind;
using cli_detail::require_object_field;
using cli_detail::require_string;

std::int64_t integer(const Json& value) {
  const auto text = require_string(value, "Int constant (decimal string)");
  std::int64_t result = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || std::to_string(result) != text)
    throw std::invalid_argument("Int constant must be canonical signed 64-bit decimal string");
  return result;
}
double floating(const Json& value) {
  if (value.kind != Kind::Number || !std::isfinite(value.number_v))
    throw std::invalid_argument("Float constant must be a finite number");
  return value.number_v;
}
bool float_range_contains(const ConstantDomain& domain, double value) {
  return std::isfinite(value) && value >= domain.float_minimum && value <= domain.float_maximum &&
      quantize_float(value,domain.float_quantization_scale) == value;
}
const std::vector<Json>& array(const Json& value) {
  if (value.kind != Kind::Array) throw std::invalid_argument("constant values must be an array");
  return value.array_v;
}
std::uint32_t length(const Json& value) {
  if (value.kind != Kind::Number || !std::isfinite(value.number_v) ||
      value.number_v < 0 || value.number_v > 65536 || std::floor(value.number_v) != value.number_v)
    throw std::invalid_argument("sequence length must be an integer in [0,65536]");
  return static_cast<std::uint32_t>(value.number_v);
}

// Conservative decoded storage bound, including an eight-byte allowance for
// each element. Only StringList -> String -> Char nesting is well typed.
std::uint64_t storage_bound(const ConstantDomain& domain) {
  if (domain.elements)
    return 8 + domain.maximum_length * storage_bound(*domain.elements);
  std::uint64_t maximum = 0;
  if (domain.type == RType::String)
    for (const auto& value : domain.values)
      maximum = std::max(maximum, static_cast<std::uint64_t>(std::get<std::string>(value).size()));
  return 8 + maximum;
}
ConstantData decode(const Json& value, RType type) {
  switch (type) {
    case RType::Int: return integer(value);
    case RType::Float: return floating(value);
    case RType::Bool:
      if (value.kind != Kind::Bool) throw std::invalid_argument("Bool constant must be boolean");
      return value.bool_v;
    case RType::Char: {
      const auto text = require_string(value, "Char constant");
      if (text.empty()) throw std::invalid_argument("Char constant must contain one Unicode scalar");
      const unsigned lead = static_cast<unsigned char>(text[0]);
      const unsigned count = lead < 0x80 ? 0 : lead >= 0xc2 && lead <= 0xdf ? 1 :
          lead >= 0xe0 && lead <= 0xef ? 2 : lead >= 0xf0 && lead <= 0xf4 ? 3 : 4;
      if (count == 4 || text.size() != count + 1)
        throw std::invalid_argument("Char constant must contain one Unicode scalar");
      unsigned code = count ? lead & ((1u << (6 - count)) - 1) : lead;
      for (unsigned i = 1; i <= count; ++i) {
        const unsigned next = static_cast<unsigned char>(text[i]);
        if ((next & 0xc0) != 0x80) throw std::invalid_argument("invalid Char UTF-8");
        code = (code << 6) | (next & 0x3f);
      }
      const unsigned minimum = count == 0 ? 0 : count == 1 ? 0x80 : count == 2 ? 0x800 : 0x10000;
      if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
        throw std::invalid_argument("Char constant must contain one Unicode scalar");
      return static_cast<char32_t>(code);
    }
    case RType::String: return require_string(value, "String constant");
    case RType::IntList: {
      std::vector<std::int64_t> result;
      for (const auto& item : array(value)) result.push_back(integer(item));
      return result;
    }
    case RType::FloatList: {
      std::vector<double> result;
      for (const auto& item : array(value)) result.push_back(floating(item));
      return result;
    }
    case RType::StringList: {
      std::vector<std::string> result;
      for (const auto& item : array(value)) result.push_back(require_string(item, "StringList element"));
      return result;
    }
    default: throw std::invalid_argument("constant type must be exact");
  }
}
}  // namespace

ConstantDomain parse_constant_domain(const cli_detail::JsonValue& definition) {
  if (definition.kind != Kind::Object) throw std::invalid_argument("constant domain must be an object");
  for (const auto& field : definition.object_v)
    if (field.first != "type" && field.first != "values" && field.first != "range" &&
        field.first != "sequence" && field.first != "quantization_scale" && field.first != "sample_from" &&
        field.first != "mutation")
      throw std::invalid_argument("constant domain: unknown key " + field.first);
  ConstantDomain result;
  result.type = parse_type(require_string(require_object_field(definition, "type"), "constant type"));
  if (definition.object_v.count("values") + definition.object_v.count("range") +
      definition.object_v.count("sequence") != 1)
    throw std::invalid_argument("constant domain requires exactly one of values, range or sequence");
  if (definition.object_v.count("sequence")) {
    const auto expected = result.type == RType::String ? RType::Char :
        result.type == RType::IntList ? RType::Int :
        result.type == RType::FloatList ? RType::Float :
        result.type == RType::StringList ? RType::String : RType::Invalid;
    if (expected == RType::Invalid)
      throw std::invalid_argument("sequence domain requires String or a typed list");
    const auto& sequence = definition.object_v.at("sequence");
    if (sequence.kind != Kind::Object) throw std::invalid_argument("sequence domain must be an object");
    for (const auto& field : sequence.object_v)
      if (field.first != "length" && field.first != "element")
        throw std::invalid_argument("sequence domain: unknown key " + field.first);
    const auto& bounds = array(require_object_field(sequence, "length"));
    if (bounds.size() != 2) throw std::invalid_argument("sequence length requires two endpoints");
    result.minimum_length = length(bounds[0]); result.maximum_length = length(bounds[1]);
    if (result.minimum_length > result.maximum_length)
      throw std::invalid_argument("sequence length endpoints reversed");
    const auto& element = require_object_field(sequence, "element");
    if (element.object_v.count("mutation"))
      throw std::invalid_argument("sequence element domains cannot declare a mutation policy");
    // Check the immediate type before recursion, so invalid nested lists cannot
    // turn a domain definition into unbounded recursive parsing.
    if (parse_type(require_string(require_object_field(element, "type"), "element type")) != expected)
      throw std::invalid_argument("sequence element type mismatch");
    result.elements = std::make_shared<const ConstantDomain>(parse_constant_domain(element));
    result.sequence_storage_bound = storage_bound(result);
    if (result.sequence_storage_bound > 16 * 1024 * 1024)
      throw std::invalid_argument("sequence domain exceeds 16 MiB decoded storage bound");
  } else if (definition.object_v.count("range")) {
    const auto& range = array(definition.object_v.at("range"));
    if (range.size() != 2) throw std::invalid_argument("constant range requires two endpoints");
    if (result.type == RType::Int) {
      result.integer_range = true;
      result.minimum = integer(range[0]); result.maximum = integer(range[1]);
      if (result.minimum > result.maximum) throw std::invalid_argument("constant range endpoints reversed");
    } else if (result.type == RType::Float) {
      result.float_range = true;
      result.float_minimum = floating(range[0]); result.float_maximum = floating(range[1]);
      if (result.float_minimum > result.float_maximum)
        throw std::invalid_argument("constant range endpoints reversed");
    } else throw std::invalid_argument("constant range requires Int or Float type");
  } else {
    for (const auto& value : array(definition.object_v.at("values"))) result.values.push_back(decode(value, result.type));
    if (result.values.empty()) throw std::invalid_argument("constant domain cannot be empty");
  }
  if (definition.object_v.count("quantization_scale")) {
    if (!result.float_range)
      throw std::invalid_argument("quantization_scale requires a Float range");
    result.float_quantization_scale = floating(definition.object_v.at("quantization_scale"));
    if (!(result.float_quantization_scale > 0) || !valid_float_quantization(
        result.float_minimum, result.float_maximum, result.float_quantization_scale))
      throw std::invalid_argument("Float quantization requires a positive scale, aligned endpoints and grid indices within 2^50");
  }
  if (definition.object_v.count("sample_from")) {
    if (!result.integer_range && !result.float_range)
      throw std::invalid_argument("sample_from requires numeric range membership");
    const auto& source = definition.object_v.at("sample_from");
    if (source.kind != Kind::Object || source.object_v.count("sample_from") || source.object_v.count("mutation"))
      throw std::invalid_argument("sample_from requires one non-nested numeric domain");
    if (parse_type(require_string(require_object_field(source,"type"),"sample type")) != result.type)
      throw std::invalid_argument("sample_from type differs from membership domain");
    auto sampling = parse_constant_domain(source);
    bool subset = true;
    if (sampling.integer_range) {
      subset = sampling.minimum >= result.minimum && sampling.maximum <= result.maximum;
    } else if (sampling.float_range) {
      subset = sampling.float_minimum >= result.float_minimum && sampling.float_maximum <= result.float_maximum;
      if (result.float_quantization_scale != 0) {
        if (sampling.float_minimum == sampling.float_maximum)
          subset = subset && float_range_contains(result,sampling.float_minimum);
        else subset = subset && sampling.float_quantization_scale == result.float_quantization_scale;
      }
    } else {
      for (const auto& value : sampling.values) {
        if (result.integer_range) {
          const auto integer_value = std::get<std::int64_t>(value);
          subset = subset && integer_value >= result.minimum && integer_value <= result.maximum;
        } else subset = subset && float_range_contains(result,std::get<double>(value));
      }
    }
    if (!subset) throw std::invalid_argument("sample_from is not a proved subset of the membership domain");
    result.sampling = std::make_shared<const ConstantDomain>(std::move(sampling));
  }
  if (definition.object_v.count("mutation")) {
    const auto& mutation = definition.object_v.at("mutation");
    if (mutation.kind == Kind::Object) {
      for (const auto& field : mutation.object_v)
        if (field.first != "kind" && field.first != "range" && field.first != "gpu_grid_steps")
          throw std::invalid_argument("add mutation: unknown key " + field.first);
      if (!mutation.object_v.count("kind") || mutation.object_v.at("kind").kind != Kind::String ||
          mutation.object_v.at("kind").string_v != "add" || !mutation.object_v.count("range"))
        throw std::invalid_argument("mutation object requires kind add and range");
      if ((!result.integer_range && !result.float_range) || result.float_quantization_scale != 0)
        throw std::invalid_argument("add requires numeric range membership without quantization");
      const auto& bounds = array(mutation.object_v.at("range"));
      if (bounds.size() != 2) throw std::invalid_argument("add delta range requires two endpoints");
      if (result.integer_range) {
        result.delta.integer_minimum = integer(bounds[0]); result.delta.integer_maximum = integer(bounds[1]);
        if (result.delta.integer_minimum > result.delta.integer_maximum || mutation.object_v.count("gpu_grid_steps"))
          throw std::invalid_argument("Int add requires ordered endpoints and no Float grid");
      } else {
        result.delta.float_minimum = floating(bounds[0]); result.delta.float_maximum = floating(bounds[1]);
        if (result.delta.float_minimum > result.delta.float_maximum)
          throw std::invalid_argument("Float add delta endpoints reversed");
        if (mutation.object_v.count("gpu_grid_steps")) {
          const auto& steps = mutation.object_v.at("gpu_grid_steps");
          if (steps.kind != Kind::Number || !std::isfinite(steps.number_v) || steps.number_v < 1 ||
              steps.number_v > UINT32_MAX || std::floor(steps.number_v) != steps.number_v)
            throw std::invalid_argument("gpu_grid_steps must be an integer in [1,4294967295]");
          result.delta.gpu_grid_steps = static_cast<std::uint32_t>(steps.number_v);
        }
      }
      result.mutation = ConstantMutationPolicy::Add;
      return result;
    }
    if (mutation.kind != Kind::String)
      throw std::invalid_argument("constant mutation policy must be a string or add object");
    const auto& policy = mutation.string_v;
    if (policy == "keep") result.mutation = ConstantMutationPolicy::Keep;
    else if (policy == "flip") {
      bool yes = false, no = false;
      if (result.type == RType::Bool) for (const auto& value : result.values) {
        if (std::get<bool>(value)) yes = true; else no = true;
      }
      if (!yes || !no) throw std::invalid_argument("flip requires a Bool domain containing both values");
      result.mutation = ConstantMutationPolicy::Flip;
    } else if (policy != "resample") throw std::invalid_argument("unknown constant mutation policy");
  }
  return result;
}

}  // namespace gagp::evo::grammar
