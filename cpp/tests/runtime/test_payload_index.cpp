#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>

#include "gagp/core/value.hpp"
#include "gagp/runtime/payload/payload.hpp"

namespace {

using gagp::Value;
using gagp::ValueTag;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

bool is_int(const Value& value, std::int64_t expected) {
  return value.tag == ValueTag::Int && value.i == expected;
}

bool test_string_bytes_and_bounds() {
  gagp::payload::clear();
  std::string bytes;
  bytes.push_back('\0');
  bytes.push_back('A');
  bytes.push_back(static_cast<char>(0xff));
  const Value key = gagp::payload::make_string_value(bytes);

  Value out = Value::invalid();
  if (!check(gagp::payload::lookup_index(key, 0, &out) &&
                 out.tag == ValueTag::Char && out.i == 0,
             "string index preserves NUL")) return false;
  if (!check(gagp::payload::lookup_index(key, 2, &out) &&
                 out.tag == ValueTag::Char && out.i == 255,
             "string index converts 0xff to unsigned Char")) return false;
  if (!check(!gagp::payload::lookup_index(key, 3, &out),
             "string index rejects the registered payload end")) return false;
  return check(!gagp::payload::lookup_index(key, 0, nullptr),
               "string index rejects a null output");
}

bool test_all_typed_lists() {
  gagp::payload::clear();
  const Value ints = gagp::payload::make_int_list_value(
      {Value::from_int(-4), Value::from_int(9)});
  const Value floats = gagp::payload::make_float_list_value(
      {Value::from_float(-1.25), Value::from_float(3.5)});
  const Value nested = gagp::payload::make_string_value("nested");
  const Value strings = gagp::payload::make_string_list_value({nested});

  Value out = Value::invalid();
  if (!check(gagp::payload::lookup_index(ints, 0, &out) && is_int(out, -4),
             "IntList returns its first stored Value")) return false;
  if (!check(gagp::payload::lookup_index(ints, 1, &out) && is_int(out, 9),
             "IntList returns its last stored Value")) return false;
  if (!check(!gagp::payload::lookup_index(ints, 2, &out),
             "IntList rejects an out-of-payload index")) return false;

  if (!check(gagp::payload::lookup_index(floats, 1, &out) &&
                 out.tag == ValueTag::Float && out.f == 3.5,
             "FloatList preserves the stored Float")) return false;
  if (!check(gagp::payload::lookup_index(strings, 0, &out) &&
                 out.tag == ValueTag::String && out.i == nested.i,
             "StringList preserves the nested String token")) return false;
  std::string exact;
  return check(gagp::payload::lookup_string(out, &exact) && exact == "nested",
               "nested String payload remains resolvable");
}

bool test_raw_values_and_malformed_registration() {
  gagp::payload::clear();
  const Value key = Value::from_int_list_hash_len(0x1234U, 5U);
  const Value raw_string = gagp::payload::make_string_value("raw");
  gagp::payload::register_list(key, {Value::from_bool(true), raw_string});

  Value out = Value::invalid();
  if (!check(gagp::payload::lookup_index(key, 0, &out) &&
                 out.tag == ValueTag::Bool && out.b,
             "lookup preserves a raw stored Value tag")) return false;
  if (!check(gagp::payload::lookup_index(key, 1, &out) &&
                 out.tag == ValueTag::String && out.i == raw_string.i,
             "lookup preserves a raw stored payload token")) return false;
  return check(!gagp::payload::lookup_index(key, 2, &out),
               "lookup bounds-checks the registered payload, not packed length");
}

bool test_missing_clear_and_noncontainer() {
  gagp::payload::clear();
  const Value missing_string = Value::from_string_hash_len(0xabcU, 1U);
  const Value missing_list = Value::from_float_list_hash_len(0xdefU, 1U);
  Value out = Value::invalid();
  if (!check(!gagp::payload::lookup_index(missing_string, 0, &out),
             "missing String token is unresolved")) return false;
  if (!check(!gagp::payload::lookup_index(missing_list, 0, &out),
             "missing list token is unresolved")) return false;
  if (!check(!gagp::payload::lookup_index(Value::from_int(3), 0, &out),
             "non-container key is unsupported")) return false;

  const Value registered = gagp::payload::make_string_value("x");
  gagp::payload::clear();
  return check(!gagp::payload::lookup_index(registered, 0, &out),
               "clear removes indexed payload lookup entries");
}

bool test_retain_only_keeps_indexed_payloads() {
  gagp::payload::clear();
  const Value kept_string = gagp::payload::make_string_value("keep");
  const Value dropped_string = gagp::payload::make_string_value("drop");
  const Value kept_list = gagp::payload::make_string_list_value({kept_string});
  const Value dropped_list = gagp::payload::make_string_list_value({dropped_string});

  gagp::payload::retain_only({kept_list});

  Value out = Value::invalid();
  if (!check(gagp::payload::lookup_index(kept_list, 0, &out) &&
                 out.tag == ValueTag::String && out.i == kept_string.i,
             "retain_only keeps indexed list payload")) return false;
  if (!check(gagp::payload::lookup_index(kept_string, 3, &out) &&
                 out.tag == ValueTag::Char && out.i == 'p',
             "retain_only keeps nested indexed String payload")) return false;
  if (!check(!gagp::payload::lookup_index(dropped_list, 0, &out),
             "retain_only removes dropped indexed list payload")) return false;
  return check(!gagp::payload::lookup_index(dropped_string, 0, &out),
               "retain_only removes dropped indexed String payload");
}

}  // namespace

int main() {
  if (!test_string_bytes_and_bounds()) return 1;
  if (!test_all_typed_lists()) return 1;
  if (!test_raw_values_and_malformed_registration()) return 1;
  if (!test_missing_clear_and_noncontainer()) return 1;
  if (!test_retain_only_keeps_indexed_payloads()) return 1;
  std::cout << "gagp_test_payload_index: OK\n";
  return 0;
}
