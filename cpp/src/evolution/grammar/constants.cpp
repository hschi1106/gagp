#include "gagp/evolution/grammar/constants.hpp"
#include "gagp/evolution/grammar/catalog.hpp"

#include <charconv>
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
const std::vector<Json>& array(const Json& value) {
  if (value.kind != Kind::Array) throw std::invalid_argument("constant values must be an array");
  return value.array_v;
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
    if (field.first != "type" && field.first != "values" && field.first != "range")
      throw std::invalid_argument("constant domain: unknown key " + field.first);
  ConstantDomain result;
  result.type = parse_type(require_string(require_object_field(definition, "type"), "constant type"));
  if (definition.object_v.count("values") == definition.object_v.count("range"))
    throw std::invalid_argument("constant domain requires exactly one of values or range");
  if (definition.object_v.count("range")) {
    if (result.type != RType::Int) throw std::invalid_argument("constant range requires Int type");
    const auto& range = array(definition.object_v.at("range"));
    if (range.size() != 2) throw std::invalid_argument("constant range requires two endpoints");
    result.integer_range = true;
    result.minimum = integer(range[0]); result.maximum = integer(range[1]);
    if (result.minimum > result.maximum) throw std::invalid_argument("constant range endpoints reversed");
  } else {
    for (const auto& value : array(definition.object_v.at("values"))) result.values.push_back(decode(value, result.type));
    if (result.values.empty()) throw std::invalid_argument("constant domain cannot be empty");
  }
  return result;
}

}  // namespace gagp::evo::grammar
