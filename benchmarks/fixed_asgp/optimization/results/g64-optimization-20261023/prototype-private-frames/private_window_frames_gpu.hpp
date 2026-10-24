#pragma once

// Isolated prototype: not included by the pinned formal0 source/binary.
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include "gagp/core/value.hpp"
#ifdef __CUDACC__
#include <cuda_runtime.h>
#define GAGP_PACKED_WINDOW_HD __host__ __device__
#else
#define GAGP_PACKED_WINDOW_HD
#endif

namespace gagp::gpu_detail {

inline constexpr unsigned kPackedWindowTagBits = 4;
inline constexpr unsigned kPackedWindowSlotBits = 5;
inline constexpr std::uint32_t kPackedWindowTagMask = 15;
inline constexpr std::uint32_t kPackedWindowSlotMask = 31;
static_assert(static_cast<unsigned>(ValueTag::Invalid) <= kPackedWindowTagMask);

// The proposed host admission must decline this layout for case/constant tags
// outside this range. All currently defined ValueTag values are representable.
GAGP_PACKED_WINDOW_HD inline bool packed_window_value_supported(Value value) {
  return static_cast<unsigned>(value.tag) <= kPackedWindowTagMask;
}

GAGP_PACKED_WINDOW_HD inline std::int64_t packed_window_value_bits(Value value) {
  if (value.tag != ValueTag::Float) return value.i;
#ifdef __CUDA_ARCH__
  return __double_as_longlong(value.f);
#else
  std::int64_t bits;
  std::memcpy(&bits, &value.f, sizeof(bits));
  return bits;
#endif
}

GAGP_PACKED_WINDOW_HD inline Value packed_window_restore(
    std::int64_t bits, ValueTag tag, bool boolean) {
  Value value;
  value.tag = tag;
  value.b = boolean;
  if (tag == ValueTag::Float) {
#ifdef __CUDA_ARCH__
    value.f = __longlong_as_double(bits);
#else
    std::memcpy(&value.f, &bits, sizeof(bits));
#endif
  } else value.i = bits;
  return value;
}

struct PackedWindowTagReference {
  std::uint32_t& tags;
  unsigned shift;
  GAGP_PACKED_WINDOW_HD PackedWindowTagReference(std::uint32_t& value, unsigned offset): tags(value), shift(offset) {}
  GAGP_PACKED_WINDOW_HD PackedWindowTagReference(const PackedWindowTagReference&) = default;
  GAGP_PACKED_WINDOW_HD operator ValueTag() const {
    return static_cast<ValueTag>((tags >> shift) & kPackedWindowTagMask);
  }
  GAGP_PACKED_WINDOW_HD PackedWindowTagReference& operator=(ValueTag value) {
    assert(static_cast<unsigned>(value) <= kPackedWindowTagMask);
    tags = (tags & ~(kPackedWindowTagMask << shift)) |
        (static_cast<std::uint32_t>(value) << shift);
    return *this;
  }
  GAGP_PACKED_WINDOW_HD PackedWindowTagReference& operator=(const PackedWindowTagReference& value) {
    return *this = static_cast<ValueTag>(value);
  }
};

struct PackedWindowBoolReference {
  std::uint32_t& tags;
  unsigned shift;
  GAGP_PACKED_WINDOW_HD PackedWindowBoolReference(std::uint32_t& value, unsigned offset): tags(value), shift(offset) {}
  GAGP_PACKED_WINDOW_HD PackedWindowBoolReference(const PackedWindowBoolReference&) = default;
  GAGP_PACKED_WINDOW_HD operator bool() const { return (tags >> shift) & 1u; }
  GAGP_PACKED_WINDOW_HD PackedWindowBoolReference& operator=(bool value) {
    tags = (tags & ~(1u << shift)) | (static_cast<std::uint32_t>(value) << shift);
    return *this;
  }
  GAGP_PACKED_WINDOW_HD PackedWindowBoolReference& operator=(const PackedWindowBoolReference& value) {
    return *this = static_cast<bool>(value);
  }
};

// Constructing a reference does not read payload or tag storage. A phase must
// retain the same lazy access to fields that have actually been initialized.
struct PackedWindowValueReference {
  std::int64_t& i;
  PackedWindowTagReference tag;
  PackedWindowBoolReference b;
  GAGP_PACKED_WINDOW_HD PackedWindowValueReference(std::int64_t& value,
      PackedWindowTagReference value_tag, PackedWindowBoolReference boolean): i(value), tag(value_tag), b(boolean) {}
  GAGP_PACKED_WINDOW_HD PackedWindowValueReference(const PackedWindowValueReference&) = default;
  GAGP_PACKED_WINDOW_HD operator Value() const {
    return packed_window_restore(i, static_cast<ValueTag>(tag), static_cast<bool>(b));
  }
  GAGP_PACKED_WINDOW_HD PackedWindowValueReference& operator=(Value value) {
    assert(packed_window_value_supported(value));
    i = packed_window_value_bits(value);
    tag = value.tag;
    b = value.b;
    return *this;
  }
  GAGP_PACKED_WINDOW_HD PackedWindowValueReference& operator=(const PackedWindowValueReference& other) {
    return *this = static_cast<Value>(other);
  }
};

struct PackedWindowSlots {
  std::int64_t* values;
  std::uint32_t& tags;
  std::size_t stride;
  unsigned first;
  GAGP_PACKED_WINDOW_HD PackedWindowValueReference operator[](unsigned index) { return reference(index); }
  GAGP_PACKED_WINDOW_HD Value operator[](unsigned index) const { return static_cast<Value>(reference(index)); }
 private:
  GAGP_PACKED_WINDOW_HD PackedWindowValueReference reference(unsigned index) const {
    const auto slot = first + index;
    const auto shift = slot * kPackedWindowSlotBits;
    return {values[static_cast<std::size_t>(slot) * stride], {tags, shift},
            {tags, shift + kPackedWindowTagBits}};
  }
};

struct PackedWindowControl {
  std::uint32_t tags;
  std::int32_t next_request;
};
static_assert(sizeof(PackedWindowControl) == sizeof(std::int64_t));
static_assert(offsetof(PackedWindowControl, next_request) == sizeof(std::uint32_t));

template<unsigned States> struct PackedPrivateWindowFrameStorage {
  static_assert(States == 1 || States == 2);
  static_assert((States + 3) * kPackedWindowSlotBits <= 32);
  std::int64_t values[States + 3];
  PackedWindowControl control;
};
static_assert(sizeof(PackedPrivateWindowFrameStorage<1>) == 40);
static_assert(sizeof(PackedPrivateWindowFrameStorage<2>) == 48);
static_assert(std::is_trivially_copyable_v<PackedPrivateWindowFrameStorage<1>>);

template<unsigned States> struct PackedPrivateWindowFrameView {
  PackedWindowSlots state, prepared, results;
  std::int32_t& next_request;
  std::uint32_t& tags;
  // Called only at original frame creation/reuse points (root, child, leaf).
  // Payload fields remain uninitialized until their original ordered writes.
  GAGP_PACKED_WINDOW_HD void initialize_tags() const { tags = 0; }
};

template<unsigned States>
GAGP_PACKED_WINDOW_HD inline PackedPrivateWindowFrameView<States> packed_window_aos_view(
    PackedPrivateWindowFrameStorage<States>& storage) {
  return {{storage.values, storage.control.tags, 1, 0},
          {storage.values, storage.control.tags, 1, States},
          {storage.values, storage.control.tags, 1, States + 1},
          storage.control.next_request, storage.control.tags};
}

// Proposed device SoA accessor: base already points at this physical lane.
// This mirrors the current window executor's block/field-plane addressing.
// Its integration and race/dirty-workspace tests remain mandatory.
template<unsigned States>
GAGP_PACKED_WINDOW_HD inline PackedPrivateWindowFrameView<States> packed_window_soa_view(
    std::int64_t* base, std::size_t frame_index, std::size_t stride) {
  auto* row = base + frame_index * (States + 4);
  auto& control = *reinterpret_cast<PackedWindowControl*>(row + (States + 3) * stride);
  return {{row, control.tags, stride, 0}, {row, control.tags, stride, States},
          {row, control.tags, stride, States + 1}, control.next_request, control.tags};
}

}  // namespace gagp::gpu_detail
#undef GAGP_PACKED_WINDOW_HD
