#pragma once

#include <algorithm>
#include <vector>
#include "gagp/runtime/gpu/device_types_gpu.hpp"

namespace gagp::gpu_detail {

// Only after complete private admission: the table contains IntList entries,
// ordered by canonical identity. Keep full-length globals canonical so their
// historical saturated/masked public lengths remain unchanged.
inline bool prepare_private_global_intlist(Value& value,
    const std::vector<DListPayloadEntry>& entries,int value_count) {
  if(value.tag!=ValueTag::IntList)return false;
  value.b=false;
  const auto entry=std::lower_bound(entries.begin(),entries.end(),value.i,
      [](const auto& candidate,std::int64_t packed){return candidate.packed<packed;});
  if(entry==entries.end() || entry->tag!=ValueTag::IntList || entry->packed!=value.i ||
      entry->len<0 || entry->len>65535 || Value::container_len(value)!=static_cast<unsigned>(entry->len) ||
      entry->offset<0 || value_count<0 || entry->offset>value_count || entry->len>value_count-entry->offset)
    return false;
  constexpr std::uint64_t global_origin=1ULL<<47;
  value=Value::from_int_list_hash_len(global_origin|static_cast<std::uint64_t>(entry->offset),entry->len);
  value.b=true;
  return true;
}

} // namespace gagp::gpu_detail
