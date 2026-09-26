#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <tuple>
#include <vector>

#include "gagp/evolution/grammar/variation_cache.hpp"
#include "gagp/evolution/grammar/variation_stats.hpp"

namespace gagp::evo::grammar {

using ContextualFrameCostTable = std::vector<std::vector<std::uint32_t>>;

struct ContextualFrameCostCacheCounters {
  std::size_t hits = 0;
  std::size_t misses = 0;
  std::size_t evictions = 0;
};

// Worker-owned context: immutable grammar/request, shared analysis registry, and
// cumulative counters. Do not share a mutable context between concurrent workers.
class VariationContext {
 public:
  explicit VariationContext(std::shared_ptr<const CompiledGrammar> grammar,
      std::size_t cache_capacity = 128,
      std::optional<ProjectedBudget> offspring_budget = std::nullopt);
  VariationContext(std::shared_ptr<const CompiledGrammar> grammar,
      GenerationRequest request, std::size_t cache_capacity = 128,
      std::optional<ProjectedBudget> offspring_budget = std::nullopt);
  VariationContext(std::shared_ptr<const CompiledGrammar> grammar,
      std::vector<GenerationRequest> requests, std::size_t cache_capacity = 128,
      std::optional<ProjectedBudget> offspring_budget = std::nullopt);
  // Optional identity is the decoded identity used by this lookup, not provenance.
  std::shared_ptr<const VariationAnalysis> analyze(const ProgramGenome& genome,
      std::string* runtime_identity = nullptr);
  const std::vector<GenerationRequest>& requests() const { return requests_; }
  const CompiledGrammar& grammar() const { return *grammar_; }
  const std::shared_ptr<const CompiledGrammar>& grammar_owner() const { return grammar_; }
  const GenerationRequest& request() const { return request_; }
  VariationAnalysisCache& cache() { return cache_; }
  VariationCounters& counters() { return counters_; }
  const VariationCounters& counters() const { return counters_; }
  // Additional admission rule for changed offspring. Parent membership remains
  // separate, allowing repair of initial trees above the variation depth limit.
  const std::optional<ProjectedBudget>& offspring_budget() const { return offspring_budget_; }
  // Internal donor-generation cache. The grammar identity is owned by this
  // context; exact local availability, effective depth and stage form the key.
  std::shared_ptr<const ContextualFrameCostTable> find_frame_costs(
      const std::vector<bool>& local_availability, std::uint32_t effective_depth_limit,
      GenerationStage stage = GenerationStage::Initial);
  std::shared_ptr<const ContextualFrameCostTable> remember_frame_costs(
      const std::vector<bool>& local_availability, std::uint32_t effective_depth_limit,
      std::shared_ptr<const ContextualFrameCostTable> costs,
      GenerationStage stage = GenerationStage::Initial);
  const ContextualFrameCostCacheCounters& frame_cost_cache_counters() const {
    return frame_cost_cache_counters_;
  }
 private:
  using FrameCostCacheKey = std::tuple<std::vector<bool>, std::uint32_t, GenerationStage>;
  std::shared_ptr<const CompiledGrammar> grammar_;
  GenerationRequest request_;
  std::vector<GenerationRequest> requests_;
  VariationAnalysisCache cache_;
  VariationCounters counters_;
  std::optional<ProjectedBudget> offspring_budget_;
  std::size_t frame_cost_cache_capacity_ = 0;
  ContextualFrameCostCacheCounters frame_cost_cache_counters_;
  std::deque<FrameCostCacheKey> frame_cost_cache_order_;
  std::map<FrameCostCacheKey, std::shared_ptr<const ContextualFrameCostTable>> frame_cost_cache_;
};

}  // namespace gagp::evo::grammar
