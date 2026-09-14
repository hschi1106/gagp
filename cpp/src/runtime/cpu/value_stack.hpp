#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "gagp/core/value.hpp"

namespace gagp::detail {

class ValueStack {
 public:
  static_assert(std::is_trivially_copyable_v<Value> &&
                std::is_trivially_destructible_v<Value>,
                "ValueStack stores non-owning VM values");
  static constexpr std::size_t k_inline_capacity = 8;

  ValueStack() = default;
  ValueStack(const ValueStack&) = delete;
  ValueStack& operator=(const ValueStack&) = delete;
  ValueStack(ValueStack&&) = delete;
  ValueStack& operator=(ValueStack&&) = delete;

  std::size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

  Value* data() { return data_; }
  const Value* data() const { return data_; }

  Value& back() {
    assert(!empty());
    return data_[size_ - 1];
  }

  const Value& back() const {
    assert(!empty());
    return data_[size_ - 1];
  }

  void push_back(Value value) {
    if (size_ == capacity_) grow();
    data_[size_] = value;
    ++size_;
  }

  void pop_back() {
    assert(!empty());
    --size_;
  }

  void truncate(std::size_t new_size) {
    assert(new_size <= size_);
    size_ = new_size;
  }

 private:
  void grow() {
    const std::size_t max_capacity = spill_.max_size();
    if (capacity_ >= max_capacity) {
      throw std::length_error("ValueStack capacity exceeds vector max_size");
    }
    const std::size_t new_capacity =
        capacity_ > max_capacity - capacity_ ? max_capacity : capacity_ * 2;
    const bool was_inline = data_ == inline_.data();
    spill_.resize(new_capacity);
    if (was_inline) std::copy_n(inline_.data(), size_, spill_.data());
    data_ = spill_.data();
    capacity_ = new_capacity;
  }

  std::array<Value, k_inline_capacity> inline_{};
  Value* data_ = inline_.data();
  std::size_t size_ = 0;
  std::size_t capacity_ = k_inline_capacity;
  std::vector<Value> spill_;
};

}  // namespace gagp::detail
