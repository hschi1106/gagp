#pragma once

#include "gagp/evolution/grammar/variation_cache.hpp"
#include "gagp/evolution/grammar/variation_stats.hpp"

namespace gagp::evo::grammar {



// Worker-owned context: immutable grammar/request, shared analysis registry, and
// cumulative counters. Do not share a mutable context between concurrent workers.
class VariationContext {
 public:
  explicit VariationContext(std::shared_ptr<const CompiledGrammar> grammar,
      std::size_t cache_capacity = 128);
  VariationContext(std::shared_ptr<const CompiledGrammar> grammar,
      GenerationRequest request, std::size_t cache_capacity = 128);
  const CompiledGrammar& grammar() const { return *grammar_; }
  const std::shared_ptr<const CompiledGrammar>& grammar_owner() const { return grammar_; }
  const GenerationRequest& request() const { return request_; }
  VariationAnalysisCache& cache() { return cache_; }
  VariationCounters& counters() { return counters_; }
  const VariationCounters& counters() const { return counters_; }
 private:
  std::shared_ptr<const CompiledGrammar> grammar_;
  GenerationRequest request_;
  VariationAnalysisCache cache_;
  VariationCounters counters_;
};

}  // namespace gagp::evo::grammar
