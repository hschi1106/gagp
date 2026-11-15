// CPU semantics of the G3P builtins: insert, remove_at, set_at, index_mod, list
// contains, range, the strip family, startswith, endswith and capitalize.
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "gagp/core/builtin.hpp"
#include "gagp/core/errors.hpp"
#include "gagp/core/value.hpp"
#include "gagp/runtime/cpu/builtins_cpu.hpp"
#include "gagp/runtime/payload/payload.hpp"
#include "../../src/runtime/payload/staging.hpp"

namespace {
using gagp::BuiltinId;
using gagp::BuiltinResult;
using gagp::ErrCode;
using gagp::Value;
using gagp::ValueTag;
namespace payload = gagp::payload;

constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();

int failures = 0;

void check(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

Value I(std::int64_t v) { return Value::from_int(v); }
Value F(double v) { return Value::from_float(v); }
Value S(const std::string& s) { return payload::make_string_value(s); }

std::vector<Value> ints(const std::vector<std::int64_t>& v) {
  std::vector<Value> out;
  for (const auto x : v) out.push_back(I(x));
  return out;
}
Value IL(const std::vector<std::int64_t>& v) { return payload::make_int_list_value(ints(v)); }
Value FL(const std::vector<double>& v) {
  std::vector<Value> out;
  for (const auto x : v) out.push_back(F(x));
  return payload::make_float_list_value(out);
}
Value SL(const std::vector<std::string>& v) {
  std::vector<Value> out;
  for (const auto& x : v) out.push_back(S(x));
  return payload::make_string_list_value(out);
}

std::vector<std::int64_t> read_ints(const Value& v) {
  std::vector<Value> elems;
  std::vector<std::int64_t> out;
  if (v.tag != ValueTag::IntList || !payload::lookup_list(v, &elems)) {
    out.push_back(kMin + 12345);  // sentinel, never a real expectation
    return out;
  }
  for (const auto& e : elems) out.push_back(e.i);
  return out;
}
std::vector<double> read_floats(const Value& v) {
  std::vector<Value> elems;
  std::vector<double> out;
  if (v.tag != ValueTag::FloatList || !payload::lookup_list(v, &elems)) return {std::nan("")};
  for (const auto& e : elems) out.push_back(e.f);
  return out;
}
std::vector<std::string> read_strings(const Value& v) {
  std::vector<Value> elems;
  std::vector<std::string> out;
  if (v.tag != ValueTag::StringList || !payload::lookup_list(v, &elems)) return {"<missing>"};
  for (const auto& e : elems) {
    std::string text;
    payload::lookup_string(e, &text);
    out.push_back(text);
  }
  return out;
}
bool is_string(const Value& v, const std::string& expected) {
  std::string text;
  return v.tag == ValueTag::String && payload::lookup_string(v, &text) && text == expected;
}

BuiltinResult call(BuiltinId id, const std::vector<Value>& args) {
  return gagp::builtin_call(id, args);
}
bool fails(BuiltinId id, const std::vector<Value>& args, ErrCode code) {
  const auto r = call(id, args);
  return r.is_error && r.err.code == code;
}

// Python's modulo for a positive modulus, without relying on the library.
std::int64_t wrap_index(std::int64_t i, std::int64_t n) {
  std::int64_t r = i % n;
  if (r < 0) r += n;
  return r;
}

void test_insert() {
  const std::vector<std::int64_t> base{1, 2, 3};
  struct Case { std::int64_t index; std::vector<std::int64_t> expected; };
  const std::vector<Case> cases{
      {0, {9, 1, 2, 3}}, {1, {1, 9, 2, 3}}, {3, {1, 2, 3, 9}}, {4, {1, 2, 3, 9}},
      {-1, {1, 2, 9, 3}}, {-3, {9, 1, 2, 3}}, {-4, {9, 1, 2, 3}}, {-100, {9, 1, 2, 3}},
      {kMin, {9, 1, 2, 3}}, {kMax, {1, 2, 3, 9}},
  };
  for (const auto& c : cases) {
    const auto r = call(BuiltinId::Insert, {IL(base), I(c.index), I(9)});
    check(!r.is_error && read_ints(r.value) == c.expected, "insert index " + std::to_string(c.index));
  }
  auto empty = call(BuiltinId::Insert, {IL({}), I(-7), I(5)});
  check(!empty.is_error && read_ints(empty.value) == std::vector<std::int64_t>{5}, "insert into empty");
  auto f = call(BuiltinId::Insert, {FL({1.5, 2.5}), I(1), F(-0.5)});
  check(!f.is_error && read_floats(f.value) == std::vector<double>({1.5, -0.5, 2.5}), "insert float");
  auto s = call(BuiltinId::Insert, {SL({"a", "c"}), I(1), S("b")});
  check(!s.is_error && read_strings(s.value) == std::vector<std::string>({"a", "b", "c"}), "insert string");
  // The source list is unchanged.
  const Value source = IL(base);
  (void)call(BuiltinId::Insert, {source, I(0), I(9)});
  check(read_ints(source) == base, "insert mutated its source");
  check(fails(BuiltinId::Insert, {IL(base), I(0), F(1.0)}, ErrCode::Type), "insert wrong element type");
  check(fails(BuiltinId::Insert, {FL({1.0}), I(0), I(1)}, ErrCode::Type), "insert int into float list");
  check(fails(BuiltinId::Insert, {SL({"a"}), I(0), I(1)}, ErrCode::Type), "insert int into string list");
  check(fails(BuiltinId::Insert, {I(1), I(0), I(1)}, ErrCode::Type), "insert non-list");
  check(fails(BuiltinId::Insert, {IL(base), F(0.0), I(1)}, ErrCode::Type), "insert float index");
  check(fails(BuiltinId::Insert, {IL(base), I(0)}, ErrCode::Type), "insert arity");
}

void test_remove_and_set() {
  const std::vector<std::int64_t> base{10, 20, 30};
  for (const std::int64_t index : {std::int64_t(0), std::int64_t(1), std::int64_t(2), std::int64_t(3),
                                   std::int64_t(4), std::int64_t(-1), std::int64_t(-3), std::int64_t(-4),
                                   std::int64_t(-100), kMin, kMax, kMin + 1}) {
    const auto position = static_cast<std::size_t>(wrap_index(index, 3));
    auto removed = base;
    removed.erase(removed.begin() + static_cast<long>(position));
    auto replaced = base;
    replaced[position] = 99;
    const auto r = call(BuiltinId::RemoveAt, {IL(base), I(index)});
    check(!r.is_error && read_ints(r.value) == removed, "remove_at index " + std::to_string(index));
    const auto s = call(BuiltinId::SetAt, {IL(base), I(index), I(99)});
    check(!s.is_error && read_ints(s.value) == replaced, "set_at index " + std::to_string(index));
  }
  const Value empty = IL({});
  const auto removed_empty = call(BuiltinId::RemoveAt, {empty, I(5)});
  check(!removed_empty.is_error && removed_empty.value.i == empty.i, "remove_at on empty list changed it");
  const auto set_empty = call(BuiltinId::SetAt, {empty, I(kMin), I(1)});
  check(!set_empty.is_error && set_empty.value.i == empty.i, "set_at on empty list changed it");
  const auto single = call(BuiltinId::RemoveAt, {IL({7}), I(kMin)});
  check(!single.is_error && read_ints(single.value).empty() && single.value.tag == ValueTag::IntList,
        "remove_at emptied a singleton");
  const auto f = call(BuiltinId::SetAt, {FL({1.0, 2.0}), I(-1), F(8.0)});
  check(!f.is_error && read_floats(f.value) == std::vector<double>({1.0, 8.0}), "set_at float");
  const auto s = call(BuiltinId::RemoveAt, {SL({"x", "y", "z"}), I(-2)});
  check(!s.is_error && read_strings(s.value) == std::vector<std::string>({"x", "z"}), "remove_at string");
  const auto ss = call(BuiltinId::SetAt, {SL({"x", "y"}), I(3), S("w")});
  check(!ss.is_error && read_strings(ss.value) == std::vector<std::string>({"x", "w"}), "set_at string");
  check(fails(BuiltinId::SetAt, {IL(base), I(0), F(1.0)}, ErrCode::Type), "set_at wrong element type");
  check(fails(BuiltinId::SetAt, {empty, I(0), F(1.0)}, ErrCode::Type), "set_at wrong type on empty list");
  check(fails(BuiltinId::SetAt, {S("ab"), I(0), I(1)}, ErrCode::Type), "set_at string source");
  check(fails(BuiltinId::SetAt, {IL(base), I(0)}, ErrCode::Type), "set_at arity");
  check(fails(BuiltinId::RemoveAt, {IL(base), F(0.0)}, ErrCode::Type), "remove_at float index");
  check(fails(BuiltinId::RemoveAt, {S("ab"), I(0)}, ErrCode::Type), "remove_at string source");
  check(fails(BuiltinId::RemoveAt, {IL(base)}, ErrCode::Type), "remove_at arity");
}

void test_index_mod() {
  const std::vector<std::int64_t> base{10, 20, 30};
  for (const std::int64_t index : {std::int64_t(0), std::int64_t(2), std::int64_t(3), std::int64_t(-1),
                                   std::int64_t(-4), kMin, kMax}) {
    const auto r = call(BuiltinId::IndexMod, {IL(base), I(index)});
    check(!r.is_error && r.value.tag == ValueTag::Int && r.value.i == base[static_cast<std::size_t>(wrap_index(index, 3))],
          "index_mod int list " + std::to_string(index));
    const auto text = call(BuiltinId::IndexMod, {S("abc"), I(index)});
    check(!text.is_error && is_string(text.value, std::string(1, "abc"[wrap_index(index, 3)])),
          "index_mod string " + std::to_string(index));
  }
  const auto zero_int = call(BuiltinId::IndexMod, {IL({}), I(kMin)});
  check(!zero_int.is_error && zero_int.value.tag == ValueTag::Int && zero_int.value.i == 0, "index_mod empty int list");
  const auto zero_float = call(BuiltinId::IndexMod, {FL({}), I(3)});
  check(!zero_float.is_error && zero_float.value.tag == ValueTag::Float && zero_float.value.f == 0.0 &&
            !std::signbit(zero_float.value.f), "index_mod empty float list");
  const auto zero_string = call(BuiltinId::IndexMod, {SL({}), I(-3)});
  check(!zero_string.is_error && is_string(zero_string.value, ""), "index_mod empty string list");
  const auto empty_text = call(BuiltinId::IndexMod, {S(""), I(kMax)});
  check(!empty_text.is_error && is_string(empty_text.value, ""), "index_mod empty string");
  const auto f = call(BuiltinId::IndexMod, {FL({0.5, 1.5}), I(-1)});
  check(!f.is_error && f.value.tag == ValueTag::Float && f.value.f == 1.5, "index_mod float list");
  const auto s = call(BuiltinId::IndexMod, {SL({"p", "qq"}), I(5)});
  check(!s.is_error && is_string(s.value, "qq"), "index_mod string list");
  check(fails(BuiltinId::IndexMod, {I(3), I(0)}, ErrCode::Type), "index_mod non-container");
  check(fails(BuiltinId::IndexMod, {IL(base), F(1.0)}, ErrCode::Type), "index_mod float index");
  check(fails(BuiltinId::IndexMod, {IL(base)}, ErrCode::Type), "index_mod arity");
}

void test_contains() {
  const auto yes = call(BuiltinId::Contains, {IL({1, 2, 3}), I(3)});
  check(!yes.is_error && yes.value.tag == ValueTag::Bool && yes.value.b, "contains int hit");
  const auto no = call(BuiltinId::Contains, {IL({1, 2, 3}), I(4)});
  check(!no.is_error && !no.value.b, "contains int miss");
  check(!call(BuiltinId::Contains, {IL({}), I(0)}).value.b, "contains in empty list");
  check(call(BuiltinId::Contains, {FL({0.0}), F(-0.0)}).value.b, "contains float equality ignores sign of zero");
  check(!call(BuiltinId::Contains, {FL({std::nan("")}), F(std::nan(""))}).value.b, "contains NaN never equal");
  check(call(BuiltinId::Contains, {FL({1.5, 2.5}), F(2.5)}).value.b, "contains float hit");
  check(call(BuiltinId::Contains, {SL({"a", "bc"}), S("bc")}).value.b, "contains string hit");
  check(!call(BuiltinId::Contains, {SL({"a", "bc"}), S("b")}).value.b, "contains string is whole-element");
  check(call(BuiltinId::Contains, {S("hello"), S("ell")}).value.b, "string contains is unchanged");
  check(fails(BuiltinId::Contains, {IL({1}), F(1.0)}, ErrCode::Type), "contains wrong element type");
  check(fails(BuiltinId::Contains, {SL({"a"}), I(1)}, ErrCode::Type), "contains int in string list");
  check(fails(BuiltinId::Contains, {S("a"), I(1)}, ErrCode::Type), "contains string with int");
  check(fails(BuiltinId::Contains, {I(1), I(1)}, ErrCode::Type), "contains non-container");
}

void test_range() {
  const auto basic = call(BuiltinId::Range, {I(0), I(5)});
  check(!basic.is_error && read_ints(basic.value) == std::vector<std::int64_t>({0, 1, 2, 3, 4}), "range 0..5");
  const auto negative = call(BuiltinId::Range, {I(-2), I(2)});
  check(!negative.is_error && read_ints(negative.value) == std::vector<std::int64_t>({-2, -1, 0, 1}), "range negative");
  for (const auto& bounds : std::vector<std::pair<std::int64_t, std::int64_t>>{
           {5, 0}, {3, 3}, {kMax, kMin}, {kMax, kMax}, {kMin, kMin}, {0, kMin}, {1, 0}}) {
    const auto r = call(BuiltinId::Range, {I(bounds.first), I(bounds.second)});
    std::vector<Value> scratch;
    check(!r.is_error && r.value.tag == ValueTag::IntList && read_ints(r.value).empty() &&
              payload::lookup_list(r.value, &scratch),
          "range reversed or empty " + std::to_string(bounds.first) + "," + std::to_string(bounds.second));
  }
  const auto low = call(BuiltinId::Range, {I(kMin), I(kMin + 3)});
  check(!low.is_error && read_ints(low.value) == std::vector<std::int64_t>({kMin, kMin + 1, kMin + 2}),
        "range at INT64_MIN");
  const auto high = call(BuiltinId::Range, {I(kMax - 2), I(kMax)});
  check(!high.is_error && read_ints(high.value) == std::vector<std::int64_t>({kMax - 2, kMax - 1}),
        "range below INT64_MAX");
  // Huge spans fail before allocating and do not overflow b-a.
  const auto before = payload::stats();
  check(fails(BuiltinId::Range, {I(kMin), I(kMax)}, ErrCode::Value), "range across the full int64 span");
  check(fails(BuiltinId::Range, {I(-1), I(kMax)}, ErrCode::Value), "range up to INT64_MAX");
  check(fails(BuiltinId::Range, {I(kMin), I(0)}, ErrCode::Value), "range from INT64_MIN to zero");
  check(fails(BuiltinId::Range, {I(0), I(65536)}, ErrCode::Value), "range beyond the list length cap");
  check(payload::stats().list_value_count == before.list_value_count, "failed range allocated cells");
  const auto longest = call(BuiltinId::Range, {I(0), I(65535)});
  check(!longest.is_error && Value::container_len(longest.value) == 65535U &&
            read_ints(longest.value).size() == 65535U && read_ints(longest.value).back() == 65534,
        "range at the length cap");
  check(fails(BuiltinId::Range, {F(0.0), I(3)}, ErrCode::Type), "range float bound");
  check(fails(BuiltinId::Range, {I(0)}, ErrCode::Type), "range arity");
  // Cells count toward the logical allocation limit exactly like concat/singleton.
  {
    payload::StagedPayloads physical(1U << 20);
    payload::StagedPayloads::Scope scope(physical);
    payload::ListAllocationScope logical(4, 0);
    check(!call(BuiltinId::Range, {I(5), I(2)}).is_error, "empty range charged the budget");
    check(!call(BuiltinId::Range, {I(0), I(3)}).is_error, "range below the allocation limit");
    bool exhausted = false;
    try { (void)call(BuiltinId::Range, {I(0), I(2)}); } catch (const payload::PayloadBudgetExceeded&) { exhausted = true; }
    check(exhausted, "range ignored the logical allocation limit");
  }
  {
    payload::StagedPayloads physical(1U << 20);
    payload::StagedPayloads::Scope scope(physical);
    payload::ListAllocationScope logical(4, 0);
    bool exhausted = false;
    try { (void)call(BuiltinId::Range, {I(0), I(5)}); } catch (const payload::PayloadBudgetExceeded&) { exhausted = true; }
    check(exhausted, "range above the allocation limit succeeded");
    bool huge = false;
    try {
      const auto r = call(BuiltinId::Range, {I(kMin), I(kMax)});
      huge = r.is_error && r.err.code == ErrCode::Value;
    } catch (const payload::PayloadBudgetExceeded&) { huge = true; }
    check(huge, "huge range under a limit was neither rejected nor an allocation error");
  }
}

void test_strip_family() {
  const std::string white = " \t\n\r\f\v";
  const std::string text = white + "ab c" + white;
  const auto both = call(BuiltinId::Strip, {S(text)});
  check(!both.is_error && is_string(both.value, "ab c"), "strip");
  check(is_string(call(BuiltinId::LStrip, {S(text)}).value, "ab c" + white), "lstrip");
  check(is_string(call(BuiltinId::RStrip, {S(text)}).value, white + "ab c"), "rstrip");
  for (const auto id : {BuiltinId::Strip, BuiltinId::LStrip, BuiltinId::RStrip}) {
    check(is_string(call(id, {S(white)}).value, ""), "strip of all-whitespace");
    check(is_string(call(id, {S("")}).value, ""), "strip of the empty string");
    check(is_string(call(id, {S("a b")}).value, "a b"), "strip keeps interior whitespace");
    check(fails(id, {I(1)}, ErrCode::Type), "strip non-string");
    check(fails(id, {S("a"), S("b")}, ErrCode::Type), "strip arity");
  }
  check(is_string(call(BuiltinId::Strip, {S(std::string("\0 a\0", 4))}).value, std::string("\0 a\0", 4)),
        "strip does not treat NUL as whitespace");
  check(is_string(call(BuiltinId::Strip, {S("\xa0z\xa0")}).value, "\xa0z\xa0"), "strip is ASCII-only");

  check(is_string(call(BuiltinId::StripChars, {S("xxabcyx"), S("xy")}).value, "abc"), "strip(chars)");
  check(is_string(call(BuiltinId::LStripChars, {S("xxabcyx"), S("xy")}).value, "abcyx"), "lstrip(chars)");
  check(is_string(call(BuiltinId::RStripChars, {S("xxabcyx"), S("xy")}).value, "xxabc"), "rstrip(chars)");
  for (const auto id : {BuiltinId::StripChars, BuiltinId::LStripChars, BuiltinId::RStripChars}) {
    check(is_string(call(id, {S(" ab "), S("")}).value, " ab "), "empty chars remove nothing");
    check(is_string(call(id, {S("aaa"), S("a")}).value, ""), "chars strip everything");
    check(is_string(call(id, {S(""), S("a")}).value, ""), "chars strip of empty text");
    check(is_string(call(id, {S("abab"), S("ba")}).value, ""), "chars are a set");
    check(fails(id, {S("a"), I(1)}, ErrCode::Type), "strip chars non-string");
    check(fails(id, {S("a")}, ErrCode::Type), "strip chars arity");
  }
  check(is_string(call(BuiltinId::StripChars, {S(" a "), S("a")}).value, " a "), "strip chars does not strip whitespace");
}

void test_affixes() {
  struct Case { std::string text, affix; bool starts, ends; };
  const std::vector<Case> cases{
      {"hello", "he", true, false}, {"hello", "lo", false, true}, {"hello", "", true, true},
      {"hello", "hello", true, true}, {"hello", "hello!", false, false}, {"", "", true, true},
      {"", "a", false, false}, {"abab", "ab", true, true}, {"abc", "bc", false, true},
  };
  for (const auto& c : cases) {
    const auto s = call(BuiltinId::StartsWith, {S(c.text), S(c.affix)});
    const auto e = call(BuiltinId::EndsWith, {S(c.text), S(c.affix)});
    check(!s.is_error && s.value.tag == ValueTag::Bool && s.value.b == c.starts, "startswith " + c.text + "/" + c.affix);
    check(!e.is_error && e.value.tag == ValueTag::Bool && e.value.b == c.ends, "endswith " + c.text + "/" + c.affix);
  }
  for (const auto id : {BuiltinId::StartsWith, BuiltinId::EndsWith}) {
    check(fails(id, {I(1), S("a")}, ErrCode::Type), "affix non-string text");
    check(fails(id, {S("a"), I(1)}, ErrCode::Type), "affix non-string affix");
    check(fails(id, {S("a")}, ErrCode::Type), "affix arity");
  }
}

void test_capitalize() {
  check(is_string(call(BuiltinId::Capitalize, {S("hELLO wORLD")}).value, "Hello world"), "capitalize");
  check(is_string(call(BuiltinId::Capitalize, {S("")}).value, ""), "capitalize empty");
  check(is_string(call(BuiltinId::Capitalize, {S("1ABC")}).value, "1abc"), "capitalize keeps a leading digit");
  check(is_string(call(BuiltinId::Capitalize, {S("a")}).value, "A"), "capitalize one character");
  check(is_string(call(BuiltinId::Capitalize, {S("\xe9\xc9")}).value, "\xe9\xc9"), "capitalize is ASCII-only");
  check(is_string(call(BuiltinId::Capitalize, {S("  ab")}).value, "  ab"), "capitalize leaves a leading space");
  check(fails(BuiltinId::Capitalize, {I(1)}, ErrCode::Type), "capitalize non-string");
  check(fails(BuiltinId::Capitalize, {S("a"), S("b")}, ErrCode::Type), "capitalize arity");
}

// Without resident contents the builtins publish the same deterministic
// hash-and-length values on both engines.
void test_fallback_payload() {
  const Value text = S("  unresolved  ");
  const Value chars = S("xy");
  const Value list = IL({1, 2, 3});
  const Value empty_list = IL({});
  const Value longer_text = S("abcdef");
  payload::clear();
  for (const auto id : {BuiltinId::Strip, BuiltinId::LStrip, BuiltinId::RStrip, BuiltinId::Capitalize}) {
    const auto r = call(id, {text});
    check(!r.is_error && r.value.tag == ValueTag::FallbackToken &&
              Value::container_len(r.value) == Value::container_len(text), "unresolved strip family token");
    const auto again = call(id, {text});
    check(again.value.i == r.value.i, "unresolved token is deterministic");
  }
  const auto a = call(BuiltinId::StripChars, {text, chars});
  const auto b = call(BuiltinId::LStripChars, {text, chars});
  check(!a.is_error && !b.is_error && a.value.i != b.value.i, "operations produce distinct tokens");
  check(fails(BuiltinId::StartsWith, {text, chars}, ErrCode::Value), "unresolved startswith");
  check(fails(BuiltinId::EndsWith, {text, chars}, ErrCode::Value), "unresolved endswith");
  check(fails(BuiltinId::Contains, {list, I(1)}, ErrCode::Value), "unresolved list contains");
  const auto insert = call(BuiltinId::Insert, {list, I(1), I(7)});
  check(!insert.is_error && insert.value.tag == ValueTag::IntList && Value::container_len(insert.value) == 4U,
        "unresolved insert length");
  const auto removed = call(BuiltinId::RemoveAt, {list, I(0)});
  check(!removed.is_error && Value::container_len(removed.value) == 2U, "unresolved remove_at length");
  const auto set = call(BuiltinId::SetAt, {list, I(0), I(7)});
  check(!set.is_error && Value::container_len(set.value) == 3U, "unresolved set_at length");
  const auto removed_empty = call(BuiltinId::RemoveAt, {empty_list, I(0)});
  check(!removed_empty.is_error && removed_empty.value.i == empty_list.i, "unresolved remove_at on empty list");
  const auto index = call(BuiltinId::IndexMod, {list, I(-1)});
  check(!index.is_error && index.value.tag == ValueTag::FallbackToken, "unresolved index_mod");
  const auto zero = call(BuiltinId::IndexMod, {empty_list, I(4)});
  check(!zero.is_error && zero.value.tag == ValueTag::Int && zero.value.i == 0, "unresolved index_mod of empty list");
  const auto letter = call(BuiltinId::IndexMod, {longer_text, I(7)});
  check(!letter.is_error && letter.value.tag == ValueTag::FallbackToken, "unresolved string index_mod");
}

}  // namespace

int main() {
  payload::clear();
  test_insert();
  test_remove_and_set();
  test_index_mod();
  test_contains();
  test_range();
  test_strip_family();
  test_affixes();
  test_capitalize();
  test_fallback_payload();
  if (failures != 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "gagp_test_g3p_builtins: OK\n";
  return 0;
}
