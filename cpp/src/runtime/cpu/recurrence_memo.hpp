#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "gagp/core/value.hpp"

namespace gagp::detail {

using RecurrenceKey = std::array<std::int64_t, 4>;

class RecurrenceMemo {
 public:
  static bool supports_limit(std::uint32_t cell_limit) {
    if (cell_limit == 0) return true;
    const std::size_t maximum = maximum_bucket_count();
    std::size_t bucket_count = 2;
    const std::size_t desired_size = static_cast<std::size_t>(cell_limit);
    if (bucket_count > maximum) return false;
    while (desired_size > bucket_count / 2) {
      if (bucket_count > maximum / 2) return false;
      bucket_count *= 2;
    }
    return true;
  }

  void reset(std::uint32_t cell_limit) {
    if (!supports_limit(cell_limit)) {
      throw std::length_error("recurrence memo cell limit is unsupported");
    }
    cell_limit_ = cell_limit;
    size_ = 0;
    for (Bucket& bucket : buckets_) bucket.occupied = false;
  }

  bool find(const RecurrenceKey& key, Value* out) const {
    const Bucket* bucket = find_bucket(key);
    if (bucket == nullptr) return false;
    if (out != nullptr) *out = bucket->value;
    return true;
  }

  bool insert(const RecurrenceKey& key, Value value) {
    if (Bucket* bucket = find_bucket(key); bucket != nullptr) {
      bucket->value = value;
      return true;
    }
    if (size_ >= cell_limit_) return false;

    ensure_capacity(size_ + 1);
    insert_without_growth(key, value, &buckets_);
    ++size_;
    return true;
  }

  std::size_t size() const { return size_; }

  std::size_t storage_bytes() const {
    return buckets_.capacity() * sizeof(Bucket);
  }

 private:
  struct Bucket {
    RecurrenceKey key{};
    Value value = Value::invalid();
    bool occupied = false;
  };

  static_assert(std::is_trivially_copyable_v<Bucket>,
                "recurrence memo buckets must have a fixed trivial layout");

  static std::size_t maximum_bucket_count() {
    const std::vector<Bucket> empty;
    const std::size_t byte_limited =
        std::numeric_limits<std::size_t>::max() / sizeof(Bucket);
    return std::min(empty.max_size(), byte_limited);
  }

  static std::uint64_t mix(std::uint64_t value) {
    value ^= value >> 30U;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27U;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31U;
    return value;
  }

  static std::size_t hash_key(const RecurrenceKey& key) {
    std::uint64_t hash = UINT64_C(0x6a09e667f3bcc909);
    for (std::size_t axis = 0; axis < key.size(); ++axis) {
      const std::uint64_t bits = static_cast<std::uint64_t>(key[axis]);
      hash ^= mix(bits + UINT64_C(0x9e3779b97f4a7c15) * (axis + 1U));
      hash = (hash << 17U) | (hash >> 47U);
      hash *= UINT64_C(0x9e3779b97f4a7c15);
    }
    return static_cast<std::size_t>(mix(hash));
  }

  static bool keys_equal(const RecurrenceKey& left,
                         const RecurrenceKey& right) {
    return left == right;
  }

  const Bucket* find_bucket(const RecurrenceKey& key) const {
    if (buckets_.empty()) return nullptr;
    const std::size_t mask = buckets_.size() - 1;
    std::size_t index = hash_key(key) & mask;
    for (;;) {
      const Bucket& bucket = buckets_[index];
      if (!bucket.occupied) return nullptr;
      if (keys_equal(bucket.key, key)) return &bucket;
      index = (index + 1) & mask;
    }
  }

  Bucket* find_bucket(const RecurrenceKey& key) {
    return const_cast<Bucket*>(
        static_cast<const RecurrenceMemo*>(this)->find_bucket(key));
  }

  static void insert_without_growth(const RecurrenceKey& key, Value value,
                                    std::vector<Bucket>* buckets) {
    const std::size_t mask = buckets->size() - 1;
    std::size_t index = hash_key(key) & mask;
    while ((*buckets)[index].occupied) index = (index + 1) & mask;
    Bucket& bucket = (*buckets)[index];
    bucket.key = key;
    bucket.value = value;
    bucket.occupied = true;
  }

  void ensure_capacity(std::size_t desired_size) {
    if (!buckets_.empty() && desired_size <= buckets_.size() / 2) return;

    const std::size_t maximum = maximum_bucket_count();
    std::size_t new_bucket_count = buckets_.empty() ? 2 : buckets_.size();
    if (new_bucket_count > maximum) {
      throw std::length_error("recurrence memo bucket count exceeds maximum");
    }
    while (desired_size > new_bucket_count / 2) {
      if (new_bucket_count > maximum / 2) {
        throw std::length_error("recurrence memo bucket growth exceeds maximum");
      }
      new_bucket_count *= 2;
    }

    std::vector<Bucket> grown(new_bucket_count);
    for (const Bucket& bucket : buckets_) {
      if (bucket.occupied) {
        insert_without_growth(bucket.key, bucket.value, &grown);
      }
    }
    buckets_.swap(grown);
  }

  std::vector<Bucket> buckets_;
  std::size_t size_ = 0;
  std::uint32_t cell_limit_ = 0;
};

}  // namespace gagp::detail
