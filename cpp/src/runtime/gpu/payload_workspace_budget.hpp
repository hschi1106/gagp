#pragma once

#include <algorithm>
#include <cstddef>

namespace gagp::gpu_detail {

// Fixed device scratch cap shared by payload evaluation and staged reproduction.
inline constexpr std::size_t kPayloadWorkspaceBytes=std::size_t(16384)*1024u*1024u;

inline std::size_t payload_workspace_block_count(std::size_t budget,
    std::size_t programs, std::size_t bytes_per_lane, std::size_t extra_bytes) {
  constexpr std::size_t threads=32;
  if(extra_bytes>=budget || bytes_per_lane==0 || bytes_per_lane>budget/threads)return 0;
  return std::min(programs,(budget-extra_bytes)/(threads*bytes_per_lane));
}

} // namespace gagp::gpu_detail
