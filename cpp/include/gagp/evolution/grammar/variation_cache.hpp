#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "gagp/evolution/grammar/variation_contract.hpp"

namespace gagp::payload { class StagedPayloads; }
namespace gagp::evo::repro { struct OwnedCompactedAnalysis; }

namespace gagp::evo::grammar {

// Internal flow handoff only: consumers must retain the same AST/request/grammar
// and validate read dependencies. This is not a certificate for mutable inputs.
struct WarmPopulationMember {
  std::shared_ptr<const VariationAnalysis> analysis;
  std::string runtime_identity;
  std::shared_ptr<payload::StagedPayloads> reads;
};

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
                                  std::size_t capacity = 128,
                                  std::optional<ProjectedBudget> projected_budget = std::nullopt);

  std::shared_ptr<const VariationAnalysis> analyze(const ProgramGenome& genome,
                                                   const GenerationRequest& request, std::string* runtime_identity = nullptr);
  std::shared_ptr<const VariationAnalysis> analyze(const ProgramGenome& genome);

  std::shared_ptr<const VariationAnalysis> analyze_member(const ProgramGenome& genome,
      const std::vector<GenerationRequest>& requests, std::string* runtime_identity = nullptr);

  // Worker-owned call: analyses run concurrently; cache/registry commits remain ordered.
  void warm_population(const std::vector<ProgramGenome>& population,
      const std::vector<GenerationRequest>& requests, std::size_t max_workers = 8,
      std::vector<WarmPopulationMember>* handoff = nullptr);

  // Speculative child analyses: no registry IDs/counters commit until analyze.
  // Invalid candidates remain on the ordinary ordered validation path.
  void warm_candidates(const std::vector<ProgramGenome>& population,
      const std::vector<GenerationRequest>& requests, std::size_t max_workers = 8);

  const CompatibilityRegistry& registry() const noexcept { return registry_; }
  const VariationCacheCounters& counters() const noexcept { return counters_; }

 private:
  friend struct gagp::evo::repro::OwnedCompactedAnalysis;
  std::string key(const std::string& identity, const GenerationRequest& request) const;
  std::string member_key(const std::string& identity,
      const std::vector<GenerationRequest>& requests) const;
  void prepare_population(const std::vector<ProgramGenome>& population,
      const std::vector<GenerationRequest>& requests, std::size_t max_workers, bool deferred,
      std::vector<WarmPopulationMember>* handoff = nullptr);
  std::shared_ptr<const VariationAnalysis> take_prepared(const std::string& key);
  std::string key(const ProgramGenome& genome, const GenerationRequest& request, std::string* runtime_identity = nullptr) const;

  std::string member_key(const ProgramGenome& genome,
      const std::vector<GenerationRequest>& requests, std::string* runtime_identity = nullptr) const;

  std::shared_ptr<const VariationAnalysis> remember(std::string key,
      std::shared_ptr<const VariationAnalysis> analysis);
  std::shared_ptr<const CompiledGrammar> grammar_;
  std::size_t capacity_ = 0;
  std::optional<ProjectedBudget> local_projected_budget_;
  std::vector<std::string> input_names_;
  std::unordered_map<std::string, std::shared_ptr<VariationAnalysis>> prepared_;
  CompatibilityRegistry registry_;
  VariationCacheCounters counters_;
  std::deque<std::string> insertion_order_;
  std::unordered_map<std::string, std::shared_ptr<const VariationAnalysis>> entries_;
};

}  // namespace gagp::evo::grammar
