#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "../../src/runtime/cpu/recurrence_memo.hpp"

namespace {

using gagp::Value;
using gagp::ValueTag;
using gagp::detail::RecurrenceKey;
using gagp::detail::RecurrenceMemo;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

bool same_value(const Value& left, const Value& right) {
  if (left.tag != right.tag) return false;
  if (left.tag == ValueTag::Invalid) return true;
  if (left.tag == ValueTag::Bool) return left.b == right.b;
  if (left.tag == ValueTag::Float) return left.f == right.f;
  return left.i == right.i;
}

bool test_collisions_and_growth() {
  // Fixed adversarial keys collide in the initial four-bucket table. Keep the
  // fixture independent of the hash implementation rather than recomputing it.
  const RecurrenceKey first{};
  const RecurrenceKey collision{2, 0, 0, 0};

  RecurrenceMemo memo;
  memo.reset(256);
  if (!check(memo.insert(first, Value::from_int(10)),
             "first colliding key inserts")) return false;
  if (!check(memo.insert(collision, Value::from_int(20)),
             "second colliding key probes to a free bucket")) return false;
  const std::size_t initial_storage = memo.storage_bytes();

  for (std::int64_t index = 2; index < 100; ++index) {
    const RecurrenceKey key{index, -index, index * 3, -index * 7};
    if (!check(memo.insert(key, Value::from_int(index)),
               "growth insertion succeeds")) return false;
  }
  if (!check(memo.size() == 100 && memo.storage_bytes() > initial_storage,
             "multiple growth rounds preserve logical size")) return false;

  Value out = Value::invalid();
  if (!check(memo.find(first, &out) && out.tag == ValueTag::Int && out.i == 10,
             "first colliding value survives growth")) return false;
  if (!check(memo.find(collision, &out) && out.tag == ValueTag::Int && out.i == 20,
             "second colliding value survives growth")) return false;
  for (std::int64_t index = 2; index < 100; ++index) {
    const RecurrenceKey key{index, -index, index * 3, -index * 7};
    if (!check(memo.find(key, &out) && out.tag == ValueTag::Int && out.i == index,
               "rehashing preserves every inserted value")) return false;
  }
  return true;
}

bool test_signed_extremes_and_all_value_tags() {
  const std::vector<Value> values{
      Value::from_int(-17),
      Value::from_float(-1.25),
      Value::from_bool(true),
      Value::from_char(255),
      Value::from_string_hash_len(UINT64_C(0x1234), 7),
      Value::from_int_list_hash_len(UINT64_C(0x2345), 8),
      Value::from_float_list_hash_len(UINT64_C(0x3456), 9),
      Value::from_string_list_hash_len(UINT64_C(0x4567), 10),
      Value::from_fallback_token(-99),
      Value::invalid(),
  };

  RecurrenceMemo memo;
  memo.reset(static_cast<std::uint32_t>(values.size() + 2));
  for (std::size_t index = 0; index < values.size(); ++index) {
    const RecurrenceKey key{
        static_cast<std::int64_t>(index),
        index % 2 == 0 ? std::numeric_limits<std::int64_t>::min()
                       : std::numeric_limits<std::int64_t>::max(),
        -static_cast<std::int64_t>(index),
        static_cast<std::int64_t>(index * 13),
    };
    if (!check(memo.insert(key, values[index]), "every Value tag inserts")) return false;
  }

  for (std::size_t index = 0; index < values.size(); ++index) {
    const RecurrenceKey key{
        static_cast<std::int64_t>(index),
        index % 2 == 0 ? std::numeric_limits<std::int64_t>::min()
                       : std::numeric_limits<std::int64_t>::max(),
        -static_cast<std::int64_t>(index),
        static_cast<std::int64_t>(index * 13),
    };
    Value out = Value::from_int(123);
    if (!check(memo.find(key, &out) && same_value(out, values[index]),
               "memo preserves the exact stored Value tag and payload")) return false;
  }
  return true;
}

bool test_limits_and_existing_update() {
  if (!check(RecurrenceMemo::supports_limit(0) &&
                 RecurrenceMemo::supports_limit(2),
             "ordinary logical limits are admissible without allocation")) return false;

  RecurrenceMemo memo;
  const RecurrenceKey first{1, 2, 3, 4};
  const RecurrenceKey second{2, 3, 4, 5};
  const RecurrenceKey third{3, 4, 5, 6};

  memo.reset(0);
  if (!check(memo.storage_bytes() == 0 && !memo.insert(first, Value::from_int(1)),
             "zero limit neither allocates nor inserts")) return false;

  memo.reset(2);
  if (!check(memo.insert(first, Value::from_int(10)) &&
                 memo.insert(second, Value::from_int(20)),
             "logical limit admits exactly two new keys")) return false;
  if (!check(!memo.insert(third, Value::from_int(30)) && memo.size() == 2,
             "new key at the logical limit is rejected")) return false;
  if (!check(memo.insert(first, Value::from_fallback_token(77)) && memo.size() == 2,
             "existing key updates at the logical limit")) return false;
  Value out = Value::invalid();
  return check(memo.find(first, &out) && out.tag == ValueTag::FallbackToken &&
                   out.i == 77,
               "full-table update stores the replacement exactly");
}

bool test_reset_and_all_key_axes() {
  RecurrenceMemo memo;
  memo.reset(64);
  const RecurrenceKey original{-1, -2, -3, -4};
  if (!check(memo.insert(original, Value::from_int(1)),
             "original key inserts")) return false;
  for (std::size_t axis = 0; axis < original.size(); ++axis) {
    RecurrenceKey changed = original;
    ++changed[axis];
    if (!check(memo.insert(changed, Value::from_int(static_cast<std::int64_t>(axis + 2))),
               "key differing in one axis inserts independently")) return false;
  }
  Value out = Value::invalid();
  for (std::size_t axis = 0; axis < original.size(); ++axis) {
    RecurrenceKey changed = original;
    ++changed[axis];
    if (!check(memo.find(changed, &out) && out.i == static_cast<std::int64_t>(axis + 2),
               "all four key slots participate in equality and hashing")) return false;
  }

  const std::size_t retained_storage = memo.storage_bytes();
  memo.reset(64);
  if (!check(memo.size() == 0 && memo.storage_bytes() == retained_storage,
             "reset clears logical entries and retains storage")) return false;
  if (!check(!memo.find(original, &out), "reset leaves no stale lookup")) return false;
  if (!check(memo.insert(original, Value::from_int(99)) &&
                 memo.storage_bytes() == retained_storage,
             "reuse within retained storage does not allocate")) return false;
  return check(memo.find(original, nullptr),
               "null output can perform a membership lookup safely");
}

}  // namespace

int main() {
  if (!test_collisions_and_growth()) return 1;
  if (!test_signed_extremes_and_all_value_tags()) return 1;
  if (!test_limits_and_existing_update()) return 1;
  if (!test_reset_and_all_key_axes()) return 1;
  std::cout << "gagp_test_recurrence_memo: OK\n";
  return 0;
}
