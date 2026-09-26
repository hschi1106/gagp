#include "gagp/evolution/grammar/values.hpp"
#include "gagp/evolution/grammar/random.hpp"
#include "gagp/evolution/grammar/numeric_sampling.hpp"
#include "gagp/runtime/payload/payload.hpp"

#include <cmath>
#include <algorithm>
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
bool same_float(double left, double right) {
  return left == right && (left != 0 || std::signbit(left) == std::signbit(right));
}
bool same_constant(const ConstantData& left, const ConstantData& right) {
  if (left.index() != right.index()) return false;
  if (const auto* value = std::get_if<double>(&left))
    return same_float(*value, std::get<double>(right));
  if (const auto* values = std::get_if<std::vector<double>>(&left)) {
    const auto& other = std::get<std::vector<double>>(right);
    return values->size() == other.size() && std::equal(values->begin(), values->end(), other.begin(), same_float);
  }
  return left == right;
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

Value sample_constant(const ConstantDomain& domain, GrammarRandom& random) {
  if (domain.sampling) return sample_constant(*domain.sampling,random);
  if (domain.integer_range)
    return Value::from_int(random.integer(domain.minimum, domain.maximum));
  if (domain.float_range)
    return Value::from_float(quantize_float(
        sample_float_interval(domain.float_minimum, domain.float_maximum, random.next()),
        domain.float_quantization_scale));
  if (!domain.elements)
    return materialize_constant(domain.type, domain.values.at(random.bounded(domain.values.size())));
  const auto count = static_cast<std::size_t>(random.integer(domain.minimum_length, domain.maximum_length));
  if (domain.type == RType::String) {
    std::string value;
    for (std::size_t i = 0; i < count; ++i)
      value += character(sample_constant(*domain.elements, random).i);
    return payload::make_string_value(value);
  }
  std::vector<Value> values;
  values.reserve(count);
  for (std::size_t i = 0; i < count; ++i) values.push_back(sample_constant(*domain.elements, random));
  if (domain.type == RType::IntList) return payload::make_int_list_value(values);
  if (domain.type == RType::FloatList) return payload::make_float_list_value(values);
  if (domain.type == RType::StringList) return payload::make_string_list_value(values);
  throw std::invalid_argument("sequence constant requires an exact sequence type");
}

Value mutate_constant_value(const ConstantDomain& domain, const Value& previous, GrammarRandom& random) {
  switch (domain.mutation) {
    case ConstantMutationPolicy::Keep: return previous;
    case ConstantMutationPolicy::Flip: return Value::from_bool(!previous.b);
    case ConstantMutationPolicy::Resample: return sample_constant(domain,random);
    case ConstantMutationPolicy::Add:
      if (domain.integer_range && previous.tag == ValueTag::Int)
        return Value::from_int(add_integer_in_range(previous.i,
            random.integer(domain.delta.integer_minimum,domain.delta.integer_maximum),domain.minimum,domain.maximum));
      if (domain.float_range && previous.tag == ValueTag::Float)
        return Value::from_float(add_float_in_range(previous.f,
            sample_float_interval(domain.delta.float_minimum,domain.delta.float_maximum,random.next()),
            domain.float_minimum,domain.float_maximum));
      break;
  }
  throw std::invalid_argument("constant mutation policy/value mismatch");
}

bool constant_domain_contains(const ConstantDomain& domain, const Value& value) {
  if (domain.integer_range)
    return value.tag == ValueTag::Int && value.i >= domain.minimum && value.i <= domain.maximum;
  if (domain.float_range)
    return value.tag == ValueTag::Float && std::isfinite(value.f) &&
        value.f >= domain.float_minimum && value.f <= domain.float_maximum &&
        quantize_float(value.f, domain.float_quantization_scale) == value.f;
  const auto contains = [&](const ConstantData& data) {
    return std::any_of(domain.values.begin(), domain.values.end(), [&](const ConstantData& allowed) {
      return same_constant(allowed, data);
    });
  };
  const auto length_fits = [&](std::size_t size) {
    return size >= domain.minimum_length && size <= domain.maximum_length;
  };
  switch (domain.type) {
    case RType::Int: return value.tag == ValueTag::Int && contains(value.i);
    case RType::Float: return value.tag == ValueTag::Float && contains(value.f);
    case RType::Bool: return value.tag == ValueTag::Bool && contains(value.b);
    case RType::Char:
      return value.tag == ValueTag::Char && value.i >= 0 && value.i <= 0x10ffff &&
          contains(static_cast<char32_t>(value.i));
    case RType::String: {
      std::string decoded;
      if (value.tag != ValueTag::String || !payload::lookup_string(value, &decoded)) return false;
      if (!domain.elements) return contains(decoded);
      std::size_t offset = 0, count = 0;
      while (offset < decoded.size()) {
        // Unicode scalars have prefix-free UTF-8 encodings. Matching the finite
        // Char alphabet rejects invalid UTF-8 as well as out-of-domain scalars.
        bool matched = false;
        for (const auto& element : domain.elements->values) {
          const auto encoded = character(std::get<char32_t>(element));
          if (decoded.compare(offset, encoded.size(), encoded) == 0) {
            offset += encoded.size(); matched = true; break;
          }
        }
        if (!matched || ++count > domain.maximum_length) return false;
      }
      return length_fits(count);
    }
    case RType::IntList:
    case RType::FloatList:
    case RType::StringList: {
      const auto tag = domain.type == RType::IntList ? ValueTag::IntList :
          domain.type == RType::FloatList ? ValueTag::FloatList : ValueTag::StringList;
      std::vector<Value> values;
      if (value.tag != tag || !payload::lookup_list(value, &values)) return false;
      if (domain.elements)
        return length_fits(values.size()) && std::all_of(values.begin(), values.end(), [&](const Value& item) {
          return constant_domain_contains(*domain.elements, item);
        });
      if (domain.type == RType::IntList) {
        std::vector<std::int64_t> decoded;
        for (const auto& item : values) {
          if (item.tag != ValueTag::Int) return false;
          decoded.push_back(item.i);
        }
        return contains(decoded);
      }
      if (domain.type == RType::FloatList) {
        std::vector<double> decoded;
        for (const auto& item : values) {
          if (item.tag != ValueTag::Float) return false;
          decoded.push_back(item.f);
        }
        return contains(decoded);
      }
      std::vector<std::string> decoded;
      for (const auto& item : values) {
        std::string text;
        if (item.tag != ValueTag::String || !payload::lookup_string(item, &text)) return false;
        decoded.push_back(std::move(text));
      }
      return contains(decoded);
    }
    default: return false;
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

std::string canonical_constant_encoding(const Value& value) {
  switch (value.tag) {
    case ValueTag::Int:
      return "{\"type\":\"Int\",\"values\":[\"" + std::to_string(value.i) + "\"]}";
    case ValueTag::Bool:
      return value.b ? "{\"type\":\"Bool\",\"values\":[true]}" :
                       "{\"type\":\"Bool\",\"values\":[false]}";
    case ValueTag::Float:
      return "{\"type\":\"Float\",\"values\":[" + canonical_json(number(value.f)) + "]}";
    case ValueTag::Char:
      return "{\"type\":\"Char\",\"values\":[" + canonical_json(string(character(value.i))) + "]}";
    case ValueTag::String:
      return "{\"type\":\"String\",\"values\":[" + canonical_json(string(payload_string(value))) + "]}";
    case ValueTag::IntList:
    case ValueTag::FloatList:
    case ValueTag::StringList: {
      std::vector<Value> values;
      if (!payload::lookup_list(value, &values))
        throw std::invalid_argument("grammar constant artifact requires exact list payload");
      const char* type = value.tag == ValueTag::IntList ? "IntList" :
          value.tag == ValueTag::FloatList ? "FloatList" : "StringList";
      std::string result = std::string("{\"type\":\"") + type + "\",\"values\":[[";
      bool first = true;
      for (const auto& element : values) {
        if (!first) result += ',';
        first = false;
        if (value.tag == ValueTag::IntList) {
          if (element.tag != ValueTag::Int)
            throw std::invalid_argument("IntList artifact element type mismatch");
          result += '"';
          result += std::to_string(element.i);
          result += '"';
        } else if (value.tag == ValueTag::FloatList) {
          if (element.tag != ValueTag::Float)
            throw std::invalid_argument("FloatList artifact element type mismatch");
          result += canonical_json(number(element.f));
        } else result += canonical_json(string(payload_string(element)));
      }
      result += "]]}";
      return result;
    }
    default:
      return canonical_json(encode_constant(value));
  }
}

Value decode_constant(const cli_detail::JsonValue& value) {
  if (value.object_v.count("mutation"))
    throw std::invalid_argument("concrete constant artifacts cannot declare a mutation policy");
  const auto domain = parse_constant_domain(value);
  if (domain.integer_range || domain.values.size() != 1)
    throw std::invalid_argument("grammar artifact constant requires one concrete value");
  return materialize_constant(domain.type, domain.values[0]);
}

}  // namespace gagp::evo::grammar
