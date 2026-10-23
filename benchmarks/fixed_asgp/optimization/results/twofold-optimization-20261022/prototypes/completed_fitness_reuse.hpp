#pragma once

#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>
#include "gagp/runtime/gpu/fitness_gpu.hpp"

namespace gagp::evo::repro {

// The GPU owner alone supplies parent maps after an exact post-elite genotype
// comparison. No AST hash, public certificate or approximate equality is used.
// A completed scalar score/counter set contains no live payload references.
class CompletedFitnessReuse {
 public:
  explicit CompletedFitnessReuse(bool enabled):enabled_(enabled) {}
  bool enabled() const {return enabled_;}
  const FitnessEvalResult& completed() const {return completed_;}
  void advance(std::vector<int> parents) {
    for(int parent:parents)
      if(parent < -1 || (parent>=0 && static_cast<std::size_t>(parent)>=parents.size()))
        throw std::logic_error("unchanged fitness parent map out of range");
    ++generation_;parents_=std::move(parents);
  }
  std::vector<int> mapping(std::uint64_t revision) const {
    if(!enabled_ || !revision || revision!=revision_ || !completed_.ok || completed_.fitness.empty())return {};
    const auto count=completed_.fitness.size();
    if(evaluated_generation_==generation_) {
      std::vector<int> identity(count);std::iota(identity.begin(),identity.end(),0);return identity;
    }
    if(evaluated_generation_+1==generation_ && parents_.size()==count)return parents_;
    return {}; // Never follow one-generation indices across an unscored gap.
  }
  void record(const FitnessEvalResult& result,std::uint64_t revision) {
    if(!enabled_)return;
    completed_=result.ok?result:FitnessEvalResult{};
    revision_=revision;evaluated_generation_=generation_;
  }
  void clear() {
    completed_=FitnessEvalResult{};revision_=0;
    evaluated_generation_=std::numeric_limits<std::uint64_t>::max();
  }
 private:
  const bool enabled_;
  std::uint64_t generation_=0,revision_=0;
  std::uint64_t evaluated_generation_=std::numeric_limits<std::uint64_t>::max();
  std::vector<int> parents_;
  FitnessEvalResult completed_;
};

} // namespace gagp::evo::repro
