#pragma once

#include <cstdint>
#include "gagp/runtime/gpu/region_types_gpu.hpp"

namespace gagp::gpu_detail {

// Capability only, after ordinary bytecode/plan verification. Every state slot
// must occur exactly once: carrying even one omitted state would alias keys.
__host__ __device__ inline unsigned dense_coordinate_memo_cells(const DRegionSegment& segment) {
  const auto capacity=segment.limits.cells;
  if(!segment.memoized || segment.progress!=RegionProgressKind::Coordinates ||
      !segment.state_count || segment.state_count>DMAX_REGION_STATES ||
      segment.coordinate_count!=segment.state_count || !capacity || capacity>DMAX_REGION_MEMO ||
      (segment.coordinate_endpoint!=DomainEndpoint::Inclusive &&
       segment.coordinate_endpoint!=DomainEndpoint::Exclusive))return 0;
  unsigned seen=0, product=1;
  for(unsigned axis=0;axis<segment.coordinate_count;++axis) {
    const auto slot=segment.coordinate_slots[axis];
    const auto& domain=segment.coordinate_domains[axis];
    if(slot>=segment.state_count || (seen&(1u<<slot)) || segment.state_types[slot]!=ValueTag::Int ||
        domain.lower.kind!=RegionBoundKind::Literal || domain.upper.kind!=RegionBoundKind::Literal ||
        domain.lower.literal>domain.upper.literal)return 0;
    seen|=1u<<slot;
    // Unsigned subtraction handles signed endpoint extremes without overflow.
    const auto width=static_cast<std::uint64_t>(domain.upper.literal)-
        static_cast<std::uint64_t>(domain.lower.literal);
    const bool inclusive=segment.coordinate_endpoint==DomainEndpoint::Inclusive;
    if(width>capacity || (inclusive && width==capacity))return 0;
    const auto extent=static_cast<unsigned>(width)+(inclusive?1u:0u);
    if(!extent || product>capacity/extent)return 0;
    product*=extent;
  }
  return product;
}

// Called only for a fully checked in-domain frame on the dense branch.
// Axis order may differ from state order; both extent and value use that axis.
template<class Frame>
__host__ __device__ inline unsigned dense_coordinate_memo_slot(const DRegionSegment& segment, const Frame& frame) {
  unsigned slot=0;
  for(unsigned axis=0;axis<segment.coordinate_count;++axis) {
    const auto& domain=segment.coordinate_domains[axis];
    const auto extent=static_cast<unsigned>(static_cast<std::uint64_t>(domain.upper.literal)-
        static_cast<std::uint64_t>(domain.lower.literal))+
        (segment.coordinate_endpoint==DomainEndpoint::Inclusive?1u:0u);
    const auto value=static_cast<unsigned>(static_cast<std::uint64_t>(frame.state[segment.coordinate_slots[axis]].i)-
        static_cast<std::uint64_t>(domain.lower.literal));
    slot=slot*extent+value;
  }
  return slot;
}

} // namespace gagp::gpu_detail
