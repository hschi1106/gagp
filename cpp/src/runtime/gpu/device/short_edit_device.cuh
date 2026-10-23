#pragma once
#include "../short_edit_distance.hpp"

// Included after the private-span definition in builtins_device.cuh.
namespace gagp::gpu_detail {
template<bool Compact>
struct DShortEditReader {
  const DPrivateIntListSpan& span;
  __device__ std::int64_t operator()(int index) const {return span.read<Compact>(index);}
};

template<bool Compact>
__device__ __noinline__ int d_short_edit_distance(const DPrivateIntListSpan& text,
    const DShortEditPattern& pattern,const DShortEditEntry* entries) {
  return short_edit_distance64(DShortEditReader<Compact>{text},text.size,
      entries?entries+pattern.offset:nullptr,pattern.count,pattern.length);
}
} // namespace gagp::gpu_detail
