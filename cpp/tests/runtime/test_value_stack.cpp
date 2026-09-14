#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <type_traits>

#include "../../src/runtime/cpu/value_stack.hpp"

namespace {

using gagp::Value;
using gagp::ValueTag;
using gagp::detail::ValueStack;

bool check(bool condition, const std::string& message) {
  if (!condition) std::cerr << "FAIL: " << message << "\n";
  return condition;
}

bool is_int(const Value& value, std::int64_t expected) {
  return value.tag == ValueTag::Int && value.i == expected;
}

bool test_inline_and_first_spill() {
  ValueStack stack;
  Value* const inline_data = stack.data();
  if (!check(stack.empty() && stack.size() == 0, "new stack is empty")) return false;

  for (std::int64_t i = 0; i < 8; ++i) {
    stack.push_back(Value::from_int(i));
    if (!check(stack.data() == inline_data, "eight values remain inline")) return false;
  }
  for (std::size_t i = 0; i < stack.size(); ++i) {
    if (!check(is_int(stack.data()[i], static_cast<std::int64_t>(i)),
               "inline values retain order")) return false;
  }

  stack.push_back(stack.back());
  if (!check(stack.data() != inline_data, "ninth value spills to heap")) return false;
  if (!check(stack.size() == 9 && is_int(stack.back(), 7),
             "aliased push survives first spill")) return false;
  for (std::size_t i = 0; i < 8; ++i) {
    if (!check(is_int(stack.data()[i], static_cast<std::int64_t>(i)),
               "first spill preserves inline values")) return false;
  }
  return true;
}

bool test_multiple_growth_and_order() {
  ValueStack stack;
  for (std::int64_t i = 0; i < 100; ++i) stack.push_back(Value::from_int(i));
  if (!check(stack.size() == 100, "multiple growth preserves logical size")) return false;
  for (std::size_t i = 0; i < stack.size(); ++i) {
    if (!check(is_int(stack.data()[i], static_cast<std::int64_t>(i)),
               "multiple growth preserves order")) return false;
  }

  for (int i = 0; i < 25; ++i) stack.pop_back();
  if (!check(stack.size() == 75 && is_int(stack.back(), 74),
             "pop_back updates the logical end")) return false;
  return true;
}

bool test_shrink_and_regrow_retains_spill() {
  ValueStack stack;
  for (std::int64_t i = 0; i < 9; ++i) stack.push_back(Value::from_int(i));
  Value* const spill_data = stack.data();

  stack.truncate(2);
  if (!check(stack.size() == 2 && is_int(stack.back(), 1),
             "truncate keeps the requested prefix")) return false;
  if (!check(stack.data() == spill_data, "truncate retains spill storage")) return false;

  for (std::int64_t i = 2; i < 16; ++i) stack.push_back(Value::from_int(100 + i));
  if (!check(stack.size() == 16 && stack.data() == spill_data,
             "regrowth within retained capacity does not reallocate")) return false;
  if (!check(is_int(stack.data()[0], 0) && is_int(stack.data()[1], 1),
             "regrowth preserves the truncated prefix")) return false;
  for (std::size_t i = 2; i < stack.size(); ++i) {
    if (!check(is_int(stack.data()[i], 100 + static_cast<std::int64_t>(i)),
               "regrowth appends in order")) return false;
  }
  return true;
}

std::int64_t sum_ints(const Value* args, std::size_t argc) {
  std::int64_t result = 0;
  for (std::size_t i = 0; i < argc; ++i) result += args[i].i;
  return result;
}

bool test_contiguous_argument_slice() {
  ValueStack stack;
  for (std::int64_t i = 1; i <= 12; ++i) stack.push_back(Value::from_int(i));
  const std::size_t argc = 4;
  const Value* const args = stack.data() + stack.size() - argc;
  if (!check(sum_ints(args, argc) == 42, "data exposes a contiguous argument slice")) return false;
  stack.truncate(stack.size() - argc);
  return check(stack.size() == 8 && is_int(stack.back(), 8),
               "argument slice can be removed after use");
}

}  // namespace

int main() {
  static_assert(!std::is_copy_constructible_v<ValueStack>);
  static_assert(!std::is_copy_assignable_v<ValueStack>);
  static_assert(!std::is_move_constructible_v<ValueStack>);
  static_assert(!std::is_move_assignable_v<ValueStack>);

  if (!test_inline_and_first_spill()) return 1;
  if (!test_multiple_growth_and_order()) return 1;
  if (!test_shrink_and_regrow_retains_spill()) return 1;
  if (!test_contiguous_argument_slice()) return 1;
  return 0;
}
