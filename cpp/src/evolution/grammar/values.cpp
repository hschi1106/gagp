#include "gagp/evolution/grammar/values.hpp"
#include "gagp/runtime/payload/payload.hpp"

#include <cmath>
#include <stdexcept>

namespace gagp::evo::grammar {
namespace {
using Json = cli_detail::JsonValue;
Json string(std::string value) { Json out; out.kind = Json::Kind::String; out.string_v = std::move(value); return out; }
Json array() { Json out; out.kind = Json::Kind::Array; return out; }
Json number(double value) {
  if (!std::isfinite(value)) throw std::invalid_argument("grammar constant artifact requires finite Float");
  Json out; out.kind = Json::Kind::Number; out.number_v = value; return out;
}
std::string character(std::int64_t value) {
  if (value < 0 || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
    throw std::invalid_argument("grammar constant artifact has invalid Char");
  const auto code = static_cast<unsigned>(value);
  std::string out;
  if (code < 0x80) out += static_cast<char>(code);
  else {
    if (code < 0x800) out += static_cast<char>(0xc0 | (code >> 6));
    else {
      if (code >= 0x10000) out += static_cast<char>(0xf0 | (code >> 18));
      out += static_cast<char>((code < 0x10000 ? 0xe0 : 0x80) | ((code >> 12) & 0x3f));
      out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
    }
    out += static_cast<char>(0x80 | (code & 0x3f));
  }
  return out;
}
std::string payload_string(const Value& value) {
  std::string decoded;
  if (value.tag != ValueTag::String || !payload::lookup_string(value, &decoded))
    throw std::invalid_argument("grammar constant artifact requires exact String payload");
  return decoded;
}
}  // namespace

Value materialize_constant(RType type, const ConstantData& data) {
  switch (type) {
    case RType::Int: return Value::from_int(std::get<std::int64_t>(data));
    case RType::Float: return Value::from_float(std::get<double>(data));
    case RType::Bool: return Value::from_bool(std::get<bool>(data));
    case RType::Char: return Value::from_char(std::get<char32_t>(data));
    case RType::String: return payload::make_string_value(std::get<std::string>(data));
    case RType::IntList: {
      std::vector<Value> values;
      for (auto item : std::get<std::vector<std::int64_t>>(data)) values.push_back(Value::from_int(item));
      return payload::make_int_list_value(values);
    }
    case RType::FloatList: {
      std::vector<Value> values;
      for (auto item : std::get<std::vector<double>>(data)) values.push_back(Value::from_float(item));
      return payload::make_float_list_value(values);
    }
    case RType::StringList: {
      std::vector<Value> values;
      for (const auto& item : std::get<std::vector<std::string>>(data)) values.push_back(payload::make_string_value(item));
      return payload::make_string_list_value(values);
    }
    default: throw std::invalid_argument("materialized constant requires exact type");
  }
}

cli_detail::JsonValue encode_constant(const Value& value) {
  Json item;
  std::string type;
  switch (value.tag) {
    case ValueTag::Int: type = "Int"; item = string(std::to_string(value.i)); break;
    case ValueTag::Float: type = "Float"; item = number(value.f); break;
    case ValueTag::Bool: type = "Bool"; item.kind = Json::Kind::Bool; item.bool_v = value.b; break;
    case ValueTag::Char: type = "Char"; item = string(character(value.i)); break;
    case ValueTag::String: type = "String"; item = string(payload_string(value)); break;
    case ValueTag::IntList:
    case ValueTag::FloatList:
    case ValueTag::StringList: {
      std::vector<Value> values;
      if (!payload::lookup_list(value, &values)) throw std::invalid_argument("grammar constant artifact requires exact list payload");
      item = array();
      type = value.tag == ValueTag::IntList ? "IntList" : value.tag == ValueTag::FloatList ? "FloatList" : "StringList";
      for (const auto& element : values) {
        if (value.tag == ValueTag::IntList) {
          if (element.tag != ValueTag::Int) throw std::invalid_argument("IntList artifact element type mismatch");
          item.array_v.push_back(string(std::to_string(element.i)));
        } else if (value.tag == ValueTag::FloatList) {
          if (element.tag != ValueTag::Float) throw std::invalid_argument("FloatList artifact element type mismatch");
          item.array_v.push_back(number(element.f));
        } else item.array_v.push_back(string(payload_string(element)));
      }
      break;
    }
    default: throw std::invalid_argument("grammar artifact rejects opaque or invalid constants");
  }
  Json out; out.kind = Json::Kind::Object;
  out.object_v["type"] = string(type);
  Json values = array(); values.array_v.push_back(std::move(item));
  out.object_v["values"] = std::move(values); return out;
}

Value decode_constant(const cli_detail::JsonValue& value) {
  const auto domain = parse_constant_domain(value);
  if (domain.integer_range || domain.values.size() != 1)
    throw std::invalid_argument("grammar artifact constant requires one concrete value");
  return materialize_constant(domain.type, domain.values[0]);
}

}  // namespace gagp::evo::grammar
