#include "gagp/evolution/repro/backend.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/crossover.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/mutation.hpp"
#include "../grammar/variation_internal.hpp"
#include "gagp/evolution/selection.hpp"

namespace gagp::evo::repro {
namespace {

ReproductionResult run_cpu_backend_impl(const std::vector<ScoredGenomeRef>& scored,
                                        const EvolutionConfig& cfg,
                                        std::mt19937_64& rng) {
  if (scored.empty()) throw std::invalid_argument("compiled reproduction requires a nonempty population");
  ReproductionResult out;
  const auto prepare_t0 = std::chrono::steady_clock::now();
  grammar::VariationContext context(cfg.compiled_grammar, population_requests(cfg), 128);
  context.defer_offspring_site_analysis(true);
  for (const auto& parent : scored) {
    if (!parent.genome) throw std::invalid_argument("compiled reproduction has a null parent");
    (void)grammar::reconstruct_population_derivation(
        context.grammar(), *parent.genome, context.requests());
  }
  out.stats.preprocess_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - prepare_t0).count();
  out.next_population.reserve(static_cast<std::size_t>(cfg.population_size));
  const int selected_count = ((cfg.population_size + 1) / 2) * 2;

  const auto selection_t0 = std::chrono::steady_clock::now();
  const bool odd_partner = cfg.population_size % 2 != 0 &&
      static_cast<std::size_t>(cfg.population_size) == scored.size();
  auto selected = tournament_selection_indices_without_replacement(
      scored, rng, cfg.selection_pressure, odd_partner ? cfg.population_size : selected_count);
  if (odd_partner) {
    const auto partner = tournament_selection_indices_without_replacement(
        scored, rng, cfg.selection_pressure, 1);
    selected.push_back(partner.front());
  }
  if (selected.size() > 1) std::shuffle(selected.begin(), selected.end(), rng);
  out.stats.selection_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - selection_t0).count();

  std::vector<std::unique_ptr<const grammar::variation_detail::CertifiedCrossoverParent>> parents(scored.size());
  const auto prepare_selected = [&](std::size_t index) {
    if (!parents[index])
      parents[index] = grammar::variation_detail::CertifiedCrossoverParent::create_scalar(
          *scored[index].genome, context);
  };
  std::uniform_real_distribution<double> probability(0.0, 1.0);
  std::uniform_int_distribution<std::uint64_t> seeds(0, UINT64_C(2000000000));
  for (std::size_t i = 0; i + 1 < selected.size() &&
       static_cast<int>(out.next_population.size()) < cfg.population_size; i += 2) {
    const auto& parent_a = *scored[selected[i]].genome;
    const auto& parent_b = *scored[selected[i + 1]].genome;
    const auto crossover_t0 = std::chrono::steady_clock::now();
    const auto crossover_seed = seeds(rng);
    prepare_selected(selected[i]);
    prepare_selected(selected[i + 1]);
    auto children = parents[selected[i]] && parents[selected[i + 1]]
        ? grammar::variation_detail::crossover_certified(*parents[selected[i]],
            *parents[selected[i + 1]], crossover_seed, context)
        : crossover(parent_a, parent_b, crossover_seed, context);
    out.stats.crossover_ms += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - crossover_t0).count();
    const auto maybe_mutate = [&](ProgramGenome child) {
      if (probability(rng) >= cfg.mutation_rate) return child;
      const auto mutation_t0 = std::chrono::steady_clock::now();
      child = mutate(child, seeds(rng), context, cfg.mutation_subtree_prob);
      out.stats.mutation_ms += std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - mutation_t0).count();
      return child;
    };
    out.next_population.push_back(maybe_mutate(std::move(children.first)));
    if (static_cast<int>(out.next_population.size()) < cfg.population_size)
      out.next_population.push_back(maybe_mutate(std::move(children.second)));
  }
  out.stats.variation = context.counters();
  return out;
}

}  // namespace

void require_reproduction_mode_supported(const EvolutionConfig& cfg) {
  if (!cfg.compiled_grammar)
    throw std::invalid_argument("evolution requires a compiled grammar");
  if (!cfg.generation_request)
    throw std::invalid_argument("evolution requires an explicit generation request");
  // The search config `execution.fuel` may lower the evaluation fuel below the
  // grammar's execution fuel, never raise it.
  const auto grammar_fuel = cfg.compiled_grammar->execution_limits().fuel;
  const bool lowered = cfg.fuel > 0 && static_cast<std::uint32_t>(cfg.fuel) < grammar_fuel;
  if (cfg.fuel <= 0 || (static_cast<std::uint32_t>(cfg.fuel) != grammar_fuel && !lowered))
    throw std::invalid_argument("compiled grammar execution fuel must match EvolutionConfig fuel");
  if (cfg.population_size <= 0)
    throw std::invalid_argument("compiled reproduction population_size must be positive");
  if (!std::isfinite(cfg.mutation_rate) || cfg.mutation_rate < 0 || cfg.mutation_rate > 1 ||
      !std::isfinite(cfg.mutation_subtree_prob) || cfg.mutation_subtree_prob < 0 ||
      cfg.mutation_subtree_prob > 1)
    throw std::invalid_argument("compiled reproduction mutation probabilities must be in [0,1]");
  (void)grammar::validate_request(*cfg.compiled_grammar, *cfg.generation_request);
}

ReproductionResult run_cpu_backend(const std::vector<ScoredGenome>& scored,
                                   const EvolutionConfig& cfg, std::mt19937_64& rng) {
  std::vector<ScoredGenomeRef> refs;
  refs.reserve(scored.size());
  for (const auto& one : scored) refs.push_back({&one.genome, one.fitness, one.wrong});
  return run_cpu_backend_impl(refs, cfg, rng);
}

ReproductionResult run_cpu_backend(const std::vector<ScoredGenomeRef>& scored,
                                   const EvolutionConfig& cfg, std::mt19937_64& rng) {
  return run_cpu_backend_impl(scored, cfg, rng);
}

// Native AST reproduction always runs on the host; the GPU reproduction
// backend selects the resident engine (evolve.cpp).
ReproductionResult run_reproduction_backend(const std::vector<ScoredGenome>& scored,
                                            const EvolutionConfig& cfg,
                                            std::mt19937_64& rng) {
  require_reproduction_mode_supported(cfg);
  return run_cpu_backend(scored, cfg, rng);
}

ReproductionResult run_reproduction_backend(const std::vector<ScoredGenomeRef>& scored,
                                            const EvolutionConfig& cfg,
                                            std::mt19937_64& rng) {
  require_reproduction_mode_supported(cfg);
  return run_cpu_backend(scored, cfg, rng);
}

}  // namespace gagp::evo::repro
