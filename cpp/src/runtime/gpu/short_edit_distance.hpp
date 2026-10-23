#pragma once
#include <cstdint>
#include "gagp/core/value.hpp"

// One-word global Levenshtein with private session-owned patterns. The positive horizontal
// boundary is one: this computes distance between whole sequences, not the
// minimum distance to a substring. Reference: Myers, JACM 1999,
// https://doi.org/10.1145/316542.316550 . Prepared labels are checked by the owning session.
#if defined(__CUDACC__)
#define GAGP_SHORT_EDIT_CALLABLE __device__
#else
#define GAGP_SHORT_EDIT_CALLABLE
#endif

namespace gagp::gpu_detail {
struct DShortEditEntry {
  std::int64_t value;
  std::uint64_t matches;
};

GAGP_SHORT_EDIT_CALLABLE inline std::uint64_t short_edit_matching_positions(
    std::int64_t value,const DShortEditEntry* entries,int count) {
  int lo=0,hi=count;
  while(lo<hi) {
    const int middle=lo+(hi-lo)/2;
    if(entries[middle].value<value)lo=middle+1;
    else hi=middle;
  }
  return lo<count && entries[lo].value==value?entries[lo].matches:0;
}

template<class Reader>
GAGP_SHORT_EDIT_CALLABLE inline int short_edit_distance64(const Reader& text,int count,
    const DShortEditEntry* pattern,int symbols,int length) {
  if(count<0 || length<0 || length>64 || symbols<0 || symbols>length)return -1;
  if(!length)return count;
  if(!count)return length;
  std::uint64_t increases=~std::uint64_t{0},decreases=0;
  const auto bottom=std::uint64_t{1}<<(length-1); // length64 shifts63, never64.
  int score=length;
  for(int i=0;i<count;++i) {
    const auto equal=short_edit_matching_positions(text(i),pattern,symbols);
    const auto vertical=equal|decreases;
    const auto diagonal=(((equal&increases)+increases)^increases)|equal;
    auto horizontal_increase=decreases|~(diagonal|increases);
    auto horizontal_decrease=increases&diagonal;
    score+=int((horizontal_increase&bottom)!=0)-int((horizontal_decrease&bottom)!=0);
    horizontal_increase=(horizontal_increase<<1)|std::uint64_t{1};
    horizontal_decrease<<=1;
    increases=horizontal_decrease|~(vertical|horizontal_increase);
    decreases=horizontal_increase&vertical;
  }
  return score;
}

struct DShortEditPattern {
  Value expected;
  int offset=0,count=0,length=-1;
};
GAGP_SHORT_EDIT_CALLABLE inline bool short_edit_pattern_matches(const DShortEditPattern& pattern,
    const Value& expected,int length,int entries) {
  return expected.tag==ValueTag::IntList && !expected.b && pattern.expected.tag==expected.tag &&
      pattern.expected.i==expected.i && pattern.expected.b==expected.b &&
      pattern.length==length && length>=0 && length<=64 && pattern.count>=0 &&
      pattern.count<=length && (!length || pattern.count>0) && pattern.offset>=0 &&
      pattern.offset<=entries && pattern.count<=entries-pattern.offset;
}
} // namespace gagp::gpu_detail
#undef GAGP_SHORT_EDIT_CALLABLE
