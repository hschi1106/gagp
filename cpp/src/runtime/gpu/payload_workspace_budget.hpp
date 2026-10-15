#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>

namespace gagp::gpu_detail {

// Decimal MiB only; no signs, whitespace, units, truncation, or wraparound.
inline bool parse_payload_workspace_budget(const char* text, std::size_t& bytes) {
  constexpr std::size_t max_mib=16384;
  std::size_t mib=512;
  if(text) {
    if(!*text)return false;
    mib=0;
    for(const char* p=text;*p;++p) {
      if(*p<'0' || *p>'9')return false;
      const unsigned digit=static_cast<unsigned>(*p-'0');
      if(mib>(max_mib-digit)/10u)return false;
      mib=mib*10+digit;
    }
    if(mib<64 || mib>max_mib)return false;
  }
  constexpr std::size_t unit=1024u*1024u;
  if(mib>std::numeric_limits<std::size_t>::max()/unit)return false;
  bytes=mib*unit;
  return true;
}

inline std::size_t payload_workspace_block_count(std::size_t budget,
    std::size_t programs, std::size_t bytes_per_lane, std::size_t extra_bytes) {
  constexpr std::size_t threads=32;
  if(extra_bytes>=budget || bytes_per_lane==0 || bytes_per_lane>budget/threads)return 0;
  return std::min(programs,(budget-extra_bytes)/(threads*bytes_per_lane));
}

} // namespace gagp::gpu_detail
