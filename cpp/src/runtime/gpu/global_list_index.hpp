#pragma once

#include <cstdint>
#include <vector>

#include "gagp/runtime/gpu/device_types_gpu.hpp"

namespace gagp::gpu_detail {

// This hash addresses immutable global entries only; it does not define public
// container identity or alter private per-invocation payload registration.
__host__ __device__ inline std::uint64_t global_list_index_hash(ValueTag tag, std::int64_t packed) {
  std::uint64_t key = static_cast<std::uint64_t>(packed) ^
      (static_cast<std::uint64_t>(tag) * 0x9e3779b97f4a7c15ULL);
  key = (key ^ (key >> 30)) * 0xbf58476d1ce4e5b9ULL;
  key = (key ^ (key >> 27)) * 0x94d049bb133111ebULL;
  return key ^ (key >> 31);
}

// Slots encode entry + 1, reserving zero for a miss. Insert in canonical table
// order and retain the first exact key, including when duplicate keys have
// different backing offsets. At most half full, with power-of-two capacity.
inline std::vector<int> build_global_list_index(const std::vector<DListPayloadEntry>& entries) {
  if (entries.empty() || entries.size() > (std::size_t(1) << 29)) return {};
  std::size_t capacity = 2;
  while (capacity < entries.size() * 2) capacity *= 2;
  std::vector<int> index(capacity, 0);
  const auto mask = capacity - 1;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    auto slot = static_cast<std::size_t>(global_list_index_hash(entry.tag, entry.packed)) & mask;
    while (index[slot]) {
      const auto& first = entries[index[slot] - 1];
      if (first.tag == entry.tag && first.packed == entry.packed) break;
      slot = (slot + 1) & mask;
    }
    if (!index[slot]) index[slot] = static_cast<int>(i) + 1;
  }
  return index;
}

}  // namespace gagp::gpu_detail
