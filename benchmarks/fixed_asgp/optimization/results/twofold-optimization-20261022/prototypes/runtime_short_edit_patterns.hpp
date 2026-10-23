#pragma once
#include "short_edit_distance.hpp"
#include <climits>
#include <map>
#include <vector>

namespace gagp::gpu_detail {
struct HostShortEditPatterns {
  std::vector<DShortEditPattern> patterns;
  std::vector<DShortEditEntry> entries;
  std::size_t eligible=0;
};

// Lookup resolves from the same immutable decoded cache uploaded by this
// session, rather than consulting the mutable registry a second time. Entries
// retain complete signed Int64 values; there is no byte alphabet or hash match.
template<class Lookup>
HostShortEditPatterns prepare_short_edit_patterns(const std::vector<Value>& answers,Lookup lookup) {
  HostShortEditPatterns result;
  if(answers.size()>static_cast<std::size_t>(INT_MAX)/64)return result;
  result.patterns.reserve(answers.size());
  for(const auto& answer:answers) {
    DShortEditPattern pattern;pattern.expected=answer;
    // Internal/private marker bits can resolve through invocation-local cells.
    // Such answers must retain ordinary resident scoring, not a registry plan.
    const auto* values=answer.tag==ValueTag::IntList && !answer.b?lookup(answer):nullptr;
    if(values && values->size()<=64) {
      bool ints=true;
      for(const auto& value:*values)ints=ints && value.tag==ValueTag::Int;
      if(ints) {
        std::map<std::int64_t,std::uint64_t> matches;
        for(std::size_t i=0;i<values->size();++i)matches[(*values)[i].i]|=std::uint64_t{1}<<i;
        pattern.length=static_cast<int>(values->size());
        pattern.count=static_cast<int>(matches.size());
        pattern.offset=static_cast<int>(result.entries.size());
        for(const auto& item:matches)result.entries.push_back({item.first,item.second});
        ++result.eligible;
      }
    }
    result.patterns.push_back(pattern);
  }
  return result;
}
} // namespace gagp::gpu_detail
