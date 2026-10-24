#pragma once

// Include only AFTER execute_bytecode_device.cuh has defined DWindowFrameAccess.
// No official source includes this file; host allocation/admission/dispatch and
// full VM/CPU parity are still required before integration.
#include "private_window_frame_views.hpp"

namespace gagp::gpu_detail {

template<unsigned States, bool SoA>
struct DWindowFrameAccess<PackedPrivateWindowLocalFrame<States>, SoA> {
  DRegionFrame* base;
  std::size_t stride;
  __device__ InitializingPackedWindowFrameView<States> operator[](std::size_t index) const {
    if constexpr (SoA)
      return initializing_packed_soa_view<States>(reinterpret_cast<std::int64_t*>(base), index, stride);
    else
      return initializing_packed_aos_view(reinterpret_cast<PackedPrivateWindowFrameStorage<States>*>(base)[index]);
  }
};

} // namespace gagp::gpu_detail
