#pragma once
#include <cstdint>

// Independent one-word global Levenshtein prototype. The positive horizontal
// boundary is one: this computes distance between whole sequences, not the
// minimum distance to a substring. Reference: Myers, JACM 1999,
// https://doi.org/10.1145/316542.316550 . No production admission yet.
#if defined(__CUDACC__)
#define GAGP_MYERS_HD __host__ __device__
#else
#define GAGP_MYERS_HD
#endif

namespace gagp::myers_prototype {
struct Entry {
  std::int64_t value;
  std::uint64_t matches;
};

GAGP_MYERS_HD inline std::uint64_t matching_positions(
    std::int64_t value,const Entry* entries,int count) {
  int lo=0,hi=count;
  while(lo<hi) {
    const int middle=lo+(hi-lo)/2;
    if(entries[middle].value<value)lo=middle+1;
    else hi=middle;
  }
  return lo<count && entries[lo].value==value?entries[lo].matches:0;
}

template<class Reader>
GAGP_MYERS_HD inline int distance64(const Reader& text,int count,
    const Entry* pattern,int symbols,int length) {
  if(count<0 || length<0 || length>64 || symbols<0 || symbols>length)return -1;
  if(!length)return count;
  if(!count)return length;
  std::uint64_t increases=~std::uint64_t{0},decreases=0;
  const auto bottom=std::uint64_t{1}<<(length-1); // length64 shifts63, never64.
  int score=length;
  for(int i=0;i<count;++i) {
    const auto equal=matching_positions(text(i),pattern,symbols);
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

struct Reader {
  const std::int64_t* values;
  GAGP_MYERS_HD std::int64_t operator()(int index) const {return values[index];}
};
} // namespace gagp::myers_prototype
#undef GAGP_MYERS_HD
