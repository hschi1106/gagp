#include "gagp/cli/json.hpp"

#include <cerrno>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace gagp::cli_detail {

JsonParser::JsonParser(std::string text, JsonParseOptions options)
    : text_(std::move(text)), options_(options) {}

JsonValue JsonParser::parse() {
  skip_ws();
  JsonValue v = parse_value();
  skip_ws();
  if (pos_ != text_.size()) {
    throw std::runtime_error("trailing characters in JSON");
  }
  return v;
}

JsonValue JsonParser::parse_value() {
  struct DepthGuard {
    std::size_t& depth;
    ~DepthGuard() { --depth; }
  } guard{depth_};
  ++depth_;
  if (options_.max_depth && depth_ > options_.max_depth)
    throw std::runtime_error("JSON nesting limit exceeded");
  if (pos_ >= text_.size()) {
    throw std::runtime_error("unexpected end of JSON");
  }
  const char c = text_[pos_];
  if (c == '{') return parse_object();
  if (c == '[') return parse_array();
  if (c == '"') return parse_string();
  if (c == 't') return parse_true();
  if (c == 'f') return parse_false();
  if (c == 'n') return parse_null();
  if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number();
  throw std::runtime_error("invalid JSON token");
}

JsonValue JsonParser::parse_object() {
  expect('{');
  JsonValue out;
  out.kind = JsonValue::Kind::Object;
  skip_ws();
  if (peek('}')) {
    expect('}');
    return out;
  }
  while (true) {
    skip_ws();
    JsonValue key = parse_string();
    skip_ws();
    expect(':');
    skip_ws();
    JsonValue val = parse_value();
    const bool inserted = out.object_v.emplace(key.string_v, std::move(val)).second;
    if (options_.strict && !inserted) throw std::runtime_error("duplicate JSON object key: " + key.string_v);
    skip_ws();
    if (peek('}')) {
      expect('}');
      break;
    }
    expect(',');
  }
  return out;
}

JsonValue JsonParser::parse_array() {
  expect('[');
  JsonValue out;
  out.kind = JsonValue::Kind::Array;
  skip_ws();
  if (peek(']')) {
    expect(']');
    return out;
  }
  while (true) {
    skip_ws();
    out.array_v.push_back(parse_value());
    skip_ws();
    if (peek(']')) {
      expect(']');
      break;
    }
    expect(',');
  }
  return out;
}

JsonValue JsonParser::parse_string() {
  expect('"');
  JsonValue out;
  out.kind = JsonValue::Kind::String;
  while (pos_ < text_.size()) {
    char c = text_[pos_++];
    if (c == '"') {
      return out;
    }
    if (c == '\\') {
      if (pos_ >= text_.size()) {
        throw std::runtime_error("invalid JSON escape");
      }
      char e = text_[pos_++];
      if (e == '"' || e == '\\' || e == '/') out.string_v.push_back(e);
      else if (e == 'b') out.string_v.push_back('\b');
      else if (e == 'f') out.string_v.push_back('\f');
      else if (e == 'n') out.string_v.push_back('\n');
      else if (e == 'r') out.string_v.push_back('\r');
      else if (e == 't') out.string_v.push_back('\t');
      else if (e == 'u' && options_.strict) {
        const auto hex4 = [&]() {
          unsigned value = 0;
          for (int i = 0; i < 4; ++i) {
            if (pos_ == text_.size()) throw std::runtime_error("short Unicode escape");
            const char h = text_[pos_++];
            unsigned digit;
            if (h >= '0' && h <= '9') digit = h - '0';
            else if (h >= 'a' && h <= 'f') digit = h - 'a' + 10;
            else if (h >= 'A' && h <= 'F') digit = h - 'A' + 10;
            else throw std::runtime_error("invalid Unicode escape");
            value = value * 16 + digit;
          }
          return value;
        };
        unsigned code = hex4();
        if (code >= 0xd800 && code <= 0xdbff) {
          if (pos_ + 2 > text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u')
            throw std::runtime_error("missing low surrogate");
          pos_ += 2;
          const unsigned low = hex4();
          if (low < 0xdc00 || low > 0xdfff) throw std::runtime_error("invalid low surrogate");
          code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
        } else if (code >= 0xdc00 && code <= 0xdfff) throw std::runtime_error("unpaired low surrogate");
        if (code < 0x80) out.string_v.push_back(static_cast<char>(code));
        else if (code < 0x800) {
          out.string_v.push_back(static_cast<char>(0xc0 | (code >> 6)));
          out.string_v.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else {
          if (code >= 0x10000) out.string_v.push_back(static_cast<char>(0xf0 | (code >> 18)));
          out.string_v.push_back(static_cast<char>((code < 0x10000 ? 0xe0 : 0x80) | ((code >> 12) & 0x3f)));
          out.string_v.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
          out.string_v.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        }
      }
      else throw std::runtime_error("unsupported JSON escape");
    } else {
      if (options_.strict && static_cast<unsigned char>(c) < 0x20)
        throw std::runtime_error("unescaped JSON control character");
      out.string_v.push_back(c);
      if (options_.strict && static_cast<unsigned char>(c) >= 0x80) {
        const unsigned lead = static_cast<unsigned char>(c);
        const unsigned count = lead >= 0xc2 && lead <= 0xdf ? 1 :
            lead >= 0xe0 && lead <= 0xef ? 2 : lead >= 0xf0 && lead <= 0xf4 ? 3 : 0;
        if (!count) throw std::runtime_error("invalid UTF-8 in JSON string");
        unsigned code = lead & ((1u << (6 - count)) - 1);
        for (unsigned i = 0; i < count; ++i) {
          if (pos_ == text_.size()) throw std::runtime_error("short UTF-8 in JSON string");
          const unsigned next = static_cast<unsigned char>(text_[pos_++]);
          if ((next & 0xc0) != 0x80) throw std::runtime_error("invalid UTF-8 continuation");
          code = (code << 6) | (next & 0x3f);
          out.string_v.push_back(static_cast<char>(next));
        }
        const unsigned minimum = count == 1 ? 0x80 : count == 2 ? 0x800 : 0x10000;
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
          throw std::runtime_error("invalid UTF-8 code point");
      }
    }
  }
  throw std::runtime_error("unterminated JSON string");
}

JsonValue JsonParser::parse_number() {
  std::size_t start = pos_;
  if (peek('-')) pos_++;
  if (peek('0')) {
    pos_++;
  } else {
    if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
      throw std::runtime_error("invalid JSON number");
    }
    while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) pos_++;
  }
  if (peek('.')) {
    pos_++;
    if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
      throw std::runtime_error("invalid JSON number fraction");
    }
    while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) pos_++;
  }
  if (peek('e') || peek('E')) {
    pos_++;
    if (peek('+') || peek('-')) pos_++;
    if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
      throw std::runtime_error("invalid JSON number exponent");
    }
    while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) pos_++;
  }
  JsonValue out;
  out.kind = JsonValue::Kind::Number;
  const std::string token = text_.substr(start, pos_ - start);
  if (options_.strict) {
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), out.number_v);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || !std::isfinite(out.number_v))
      throw std::runtime_error("JSON number out of range");
    return out;
  }
  errno = 0;
  char* end = nullptr;
  out.number_v = std::strtod(token.c_str(), &end);
  if (end != token.c_str() + token.size()) {
    throw std::runtime_error("invalid JSON number");
  }
  if (errno == ERANGE && !std::isfinite(out.number_v)) {
    throw std::runtime_error("JSON number out of range");
  }
  return out;
}

JsonValue JsonParser::parse_true() {
  expect_word("true");
  JsonValue out;
  out.kind = JsonValue::Kind::Bool;
  out.bool_v = true;
  return out;
}

JsonValue JsonParser::parse_false() {
  expect_word("false");
  JsonValue out;
  out.kind = JsonValue::Kind::Bool;
  out.bool_v = false;
  return out;
}

JsonValue JsonParser::parse_null() {
  expect_word("null");
  return JsonValue{};
}

void JsonParser::expect(char c) {
  if (pos_ >= text_.size() || text_[pos_] != c) {
    throw std::runtime_error("unexpected JSON character");
  }
  pos_++;
}

void JsonParser::expect_word(const char* word) {
  while (*word) {
    expect(*word);
    word++;
  }
}

bool JsonParser::peek(char c) const { return pos_ < text_.size() && text_[pos_] == c; }

void JsonParser::skip_ws() {
  while (pos_ < text_.size()) {
    const char c = text_[pos_];
    const bool whitespace = options_.strict ? (c == ' ' || c == '\t' || c == '\n' || c == '\r') :
        std::isspace(static_cast<unsigned char>(c)) != 0;
    if (!whitespace) break;
    ++pos_;
  }
}

const JsonValue& require_object_field(const JsonValue& obj, const char* key) {
  if (obj.kind != JsonValue::Kind::Object) {
    throw std::runtime_error("expected object");
  }
  auto it = obj.object_v.find(key);
  if (it == obj.object_v.end()) {
    throw std::runtime_error(std::string("missing field: ") + key);
  }
  return it->second;
}

int require_int(const JsonValue& v, const char* field_name) {
  if (v.kind != JsonValue::Kind::Number) {
    throw std::runtime_error(std::string("expected number field: ") + field_name);
  }
  const long long i = static_cast<long long>(v.number_v);
  if (static_cast<double>(i) != v.number_v) {
    throw std::runtime_error(std::string("expected integer number field: ") + field_name);
  }
  return static_cast<int>(i);
}

std::string require_string(const JsonValue& v, const char* field_name) {
  if (v.kind != JsonValue::Kind::String) {
    throw std::runtime_error(std::string("expected string field: ") + field_name);
  }
  return v.string_v;
}

}  // namespace gagp::cli_detail
