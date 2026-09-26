#include "gagp/evolution/grammar/variation_cache.hpp"

#include <stdexcept>
#include <algorithm>
#include <atomic>
#include <future>
#include <thread>
#include <string_view>

#include "gagp/evolution/grammar/cache.hpp"
#include "gagp/evolution/grammar/derivation_resources.hpp"
#include "../../runtime/payload/staging.hpp"

namespace gagp::evo::grammar {
namespace {

void append_field(std::string& output, std::string_view value) {
  output += std::to_string(value.size());
  output += ':';
  output.append(value.data(), value.size());
}

void append_number(std::string& output, std::uint32_t value) {
  append_field(output, std::to_string(value));
}

}  // namespace

VariationAnalysisCache::VariationAnalysisCache(
    std::shared_ptr<const CompiledGrammar> grammar, std::size_t capacity,
    std::optional<ProjectedBudget> projected_budget)
    : grammar_(std::move(grammar)), capacity_(capacity) {
  if (!grammar_) throw std::invalid_argument("variation analysis cache requires a grammar");
  if (capacity_ == 0) throw std::invalid_argument("variation analysis cache capacity must be positive");
  if (projected_budget && resource_charges_are_local(*grammar_)) local_projected_budget_ = projected_budget;
  input_names_.reserve(grammar_->inputs().size());
  for (const auto& input : grammar_->inputs()) input_names_.push_back(input.name);
  entries_.reserve(capacity_);
}

std::string VariationAnalysisCache::key(
    const ProgramGenome& genome, const GenerationRequest& request, std::string* runtime_identity) const {
  std::string result;
  append_field(result, "grammar-variation-analysis-cache-v1");
  append_field(result, grammar_->content_hash());
  const auto identity = runtime_cache_identity(genome, input_names_, grammar_->execution_limits().fuel);
  append_field(result, identity);
  if (runtime_identity) *runtime_identity = identity;
  append_number(result, request.nonterminal);
  append_number(result, static_cast<std::uint32_t>(request.type));
  append_number(result, static_cast<std::uint32_t>(request.visible_environment.size()));
  for (const auto& binding : request.visible_environment) {
    append_field(result, binding.name);
    append_number(result, static_cast<std::uint32_t>(binding.type));
  }
  append_number(result, request.budget.max_nodes);
  append_number(result, request.budget.max_depth);
  append_number(result, static_cast<std::uint32_t>(request.stage));
  return result;
}

std::shared_ptr<const VariationAnalysis> VariationAnalysisCache::analyze(
    const ProgramGenome& genome, const GenerationRequest& request, std::string* runtime_identity) {
  // Invalid requests never participate in cache lookup or accounting.
  (void)validate_request(*grammar_, request);
  const auto cache_key = key(genome, request, runtime_identity);
  const auto found = entries_.find(cache_key);
  if (found != entries_.end()) {
    ++counters_.hits;
    return found->second;
  }

  ++counters_.misses;
  if (auto prepared = take_prepared(cache_key)) return remember(cache_key, std::move(prepared));
  auto analysis = std::make_shared<const VariationAnalysis>(
      analyze_variation(*grammar_, genome, request, &registry_,
          local_projected_budget_ ? &*local_projected_budget_ : nullptr));
  return remember(cache_key, std::move(analysis));
}

std::shared_ptr<const VariationAnalysis> VariationAnalysisCache::remember(
    std::string cache_key, std::shared_ptr<const VariationAnalysis> analysis) {
  if (entries_.size() == capacity_) {
    entries_.erase(insertion_order_.front());
    insertion_order_.pop_front();
    ++counters_.evictions;
  }
  insertion_order_.push_back(cache_key);
  entries_.emplace(std::move(cache_key), analysis);
  return analysis;
}

std::string VariationAnalysisCache::member_key(const ProgramGenome& genome,
    const std::vector<GenerationRequest>& requests, std::string* runtime_identity) const {
  auto cache_key = key(genome, requests.front(), runtime_identity);
  if (requests.size() == 1) return cache_key;
  append_field(cache_key, "population-root-requests-v1");
  append_number(cache_key, static_cast<std::uint32_t>(requests.size()));
  for (std::size_t i = 1; i < requests.size(); ++i) {
    append_number(cache_key, requests[i].nonterminal);
    append_number(cache_key, static_cast<std::uint32_t>(requests[i].type));
    append_number(cache_key, static_cast<std::uint32_t>(requests[i].stage));
    // Validation requires closed roots and equal budgets, already in the key.
  }
  return cache_key;
}

std::shared_ptr<const VariationAnalysis> VariationAnalysisCache::take_prepared(const std::string& key) {
  const auto found = prepared_.find(key);
  if (found == prepared_.end()) return nullptr;
  auto analysis = std::move(found->second);
  prepared_.erase(found);
  for (auto& site : analysis->sites) site.compatibility_id = registry_.intern(site.compatibility_key);
  return analysis;
}

void VariationAnalysisCache::warm_population(const std::vector<ProgramGenome>& population,
    const std::vector<GenerationRequest>& requests, std::size_t max_workers,
    std::vector<WarmPopulationMember>* handoff) {
  prepare_population(population, requests, max_workers, false, handoff);
}

void VariationAnalysisCache::warm_candidates(const std::vector<ProgramGenome>& population,
    const std::vector<GenerationRequest>& requests, std::size_t max_workers) {
  prepare_population(population, requests, max_workers, true);
}

void VariationAnalysisCache::prepare_population(const std::vector<ProgramGenome>& population,
    const std::vector<GenerationRequest>& requests, std::size_t max_workers, bool deferred,
    std::vector<WarmPopulationMember>* handoff) {
  if (handoff) {
    handoff->clear();
    if (population.size() > capacity_) handoff = nullptr;
    else handoff->resize(population.size());
  }
  validate_population_requests(*grammar_, requests);
  if (!max_workers) throw std::invalid_argument("analysis worker limit must be positive");
  const auto workers = std::min<std::size_t>(max_workers,
      std::max(1u, std::thread::hardware_concurrency()));
  if (deferred) prepared_.clear();
  // Worker threads cannot see an enclosing thread's uncommitted payloads.
  // Deferred warming is optional; ordinary warming uses the active view in order.
  if (payload::StagedPayloads::has_active_scope()) {
    if (!deferred)
      for (const auto& genome : population) (void)analyze_member(genome, requests);
    return;
  }
  if (!deferred && (workers == 1 || population.size() < 32)) {
    for (const auto& genome : population) (void)analyze_member(genome, requests);
    return;
  }
  // Bound speculative results and join every reader before returning to generation.
  constexpr std::size_t batch_size = 128;
  for (std::size_t begin = 0; begin < population.size(); begin += batch_size) {
    const auto count = std::min(batch_size, population.size() - begin);
    std::vector<std::string> keys(count);
    std::vector<std::string> identities(handoff ? count : 0);
    std::vector<std::shared_ptr<const VariationAnalysis>> cached(count);
    std::vector<std::shared_ptr<VariationAnalysis>> computed(count);
    std::vector<std::exception_ptr> errors(count);
    std::vector<std::unique_ptr<payload::StagedPayloads>> snapshots(count);
    std::atomic<std::size_t> next{0};
    std::vector<std::future<void>> pending;
    for (std::size_t worker = 0; worker < std::min(workers, count); ++worker)
      pending.push_back(std::async(std::launch::async, [&] {
        for (;;) {
          const auto i = next.fetch_add(1, std::memory_order_relaxed);
          if (i >= count) break;
          snapshots[i] = std::make_unique<payload::StagedPayloads>();
          try {
            // Identity and analysis must see the SAME decoded values. Repeated
            // membership reads then reuse this private snapshot without locking
            // the global payload registry for every constant/domain query.
            payload::StagedPayloads::Scope scope(*snapshots[i]);
            keys[i] = member_key(population[begin + i], requests, handoff ? &identities[i] : nullptr);
            const auto found = entries_.find(keys[i]);
            if (found != entries_.end()) { cached[i] = found->second; continue; }
            computed[i] = std::make_shared<VariationAnalysis>(analyze_population_variation(
                *grammar_, population[begin + i], requests, nullptr,
                local_projected_budget_ ? &*local_projected_budget_ : nullptr));
          } catch (...) { errors[i] = std::current_exception(); }
        }
      }));
    for (auto& task : pending) task.get();
    std::vector<payload::StagedPayloads*> transactions;
    for (const auto& snapshot : snapshots) transactions.push_back(snapshot.get());
    if (!payload::StagedPayloads::commit_all(transactions)) {
      // No cache, registry, or counter changes have committed from this batch.
      // Replay ordinary warming against current payloads; deferred work may be
      // discarded and revalidated when the candidate is actually consumed.
      if (!deferred)
        for (std::size_t i = 0; i < count; ++i)
          (void)analyze_member(population[begin + i], requests);
      continue;
    }
    for (std::size_t i = 0; i < count; ++i) {
      if (deferred) {
        if (!errors[i] && computed[i] && prepared_.size() < std::min(capacity_, batch_size))
          prepared_.emplace(keys[i], std::move(computed[i]));
        continue;
      }
      if (errors[i]) std::rethrow_exception(errors[i]);
      const auto found = entries_.find(keys[i]);
      std::shared_ptr<const VariationAnalysis> analysis;
      if (found != entries_.end()) {
        ++counters_.hits;
        analysis = found->second;
      } else {
        ++counters_.misses;
        if (computed[i]) {
          for (auto& site : computed[i]->sites)
            site.compatibility_id = registry_.intern(site.compatibility_key);
          analysis = remember(std::move(keys[i]), std::move(computed[i]));
        } else {
          // A previously cached entry may have been evicted earlier in this batch.
          analysis = remember(std::move(keys[i]), std::move(cached[i]));
        }
      }
      if (handoff) (*handoff)[begin + i] = {
          std::move(analysis), std::move(identities[i]), std::move(snapshots[i])};
    }
  }
}

std::shared_ptr<const VariationAnalysis> VariationAnalysisCache::analyze_member(
    const ProgramGenome& genome, const std::vector<GenerationRequest>& requests, std::string* runtime_identity) {
  validate_population_requests(*grammar_, requests);
  if (requests.size() == 1) return analyze(genome, requests.front(), runtime_identity);
  auto cache_key = member_key(genome, requests, runtime_identity);
  const auto found = entries_.find(cache_key);
  if (found != entries_.end()) {
    ++counters_.hits;
    return found->second;
  }
  ++counters_.misses;
  if (auto prepared = take_prepared(cache_key)) return remember(cache_key, std::move(prepared));
  return remember(std::move(cache_key), std::make_shared<const VariationAnalysis>(
      analyze_population_variation(*grammar_, genome, requests, &registry_,
          local_projected_budget_ ? &*local_projected_budget_ : nullptr)));
}

std::shared_ptr<const VariationAnalysis> VariationAnalysisCache::analyze(
    const ProgramGenome& genome) {
  return analyze(genome, entry_request(*grammar_));
}

}  // namespace gagp::evo::grammar
