#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "gagp/evolution/grammar/variation_contract.hpp"

namespace gagp::evo::grammar {

struct VariationCacheCounters {
  std::size_t hits = 0;
  std::size_t misses = 0;
  std::size_t evictions = 0;
};

// Bounded analysis cache for one immutable compiled grammar. Compatibility IDs
// are stable for the cache lifetime and are meaningful only within its registry.
class VariationAnalysisCache {
 public:
  explicit VariationAnalysisCache(std::shared_ptr<const CompiledGrammar> grammar,
                                  std::size_t capacity = 128);

  std::shared_ptr<const VariationAnalysis> analyze(const ProgramGenome& genome,
                                                   const GenerationRequest& request);
  std::shared_ptr<const VariationAnalysis> analyze(const ProgramGenome& genome);

  const CompatibilityRegistry& registry() const noexcept { return registry_; }
  const VariationCacheCounters& counters() const noexcept { return counters_; }

 private:
  std::string key(const ProgramGenome& genome, const GenerationRequest& request) const;

  std::shared_ptr<const CompiledGrammar> grammar_;
  std::size_t capacity_ = 0;
  std::vector<std::string> input_names_;
  CompatibilityRegistry registry_;
  VariationCacheCounters counters_;
  std::deque<std::string> insertion_order_;
  std::unordered_map<std::string, std::shared_ptr<const VariationAnalysis>> entries_;
};

}  // namespace gagp::evo::grammar
