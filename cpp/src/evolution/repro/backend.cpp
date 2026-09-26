#include "gagp/evolution/repro/backend.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/crossover.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/mutation.hpp"
#include "gagp/evolution/repro/gpu.hpp"
#include "gagp/evolution/repro/prep.hpp"
#include "../grammar/variation_internal.hpp"
#include "gagp/evolution/selection.hpp"

namespace gagp::evo::repro {
namespace {

std::uint64_t hash64(std::uint64_t x) {
  x ^= x >> 30; x *= UINT64_C(0xbf58476d1ce4e5b9);
  x ^= x >> 27; x *= UINT64_C(0x94d049bb133111eb);
  return x ^ (x >> 31);
}

int gcd_int(int a, int b) {
  while (b != 0) { const int next = a % b; a = b; b = next; }
  return a < 0 ? -a : a;
}

int choose_stride(int size, std::uint64_t seed) {
  if (size <= 1) return 1;
  int stride = static_cast<int>(hash64(seed ^ UINT64_C(0x94d049bb133111eb)) %
                                static_cast<std::uint64_t>(size));
  if (stride <= 0) stride = 1;
  while (gcd_int(stride, size) != 1) if (++stride >= size) stride = 1;
  return stride;
}

int permuted_index(int size, int logical, std::uint64_t seed) {
  if (size <= 1) return 0;
  const int offset = static_cast<int>(hash64(seed ^ UINT64_C(0xbf58476d1ce4e5b9)) %
                                      static_cast<std::uint64_t>(size));
  return (offset + logical * choose_stride(size, seed)) % size;
}

std::vector<std::size_t> gpu_style_selection_indices(
    const std::vector<ScoredGenomeRef>& scored, int pressure, int count,
    std::uint64_t selection_seed) {
  if (scored.empty()) throw std::invalid_argument("scored population is empty");
  const int size = static_cast<int>(scored.size());
  const int k = std::max(1, std::min(size, pressure));
  const int winners_per_round = (size + k - 1) / k;
  std::vector<std::size_t> selected;
  selected.reserve(static_cast<std::size_t>(count));
  for (int slot = 0; slot < count; ++slot) {
    const int round = slot / winners_per_round;
    const int begin = (slot % winners_per_round) * k;
    const int end = std::min(size, begin + k);
    const auto seed = hash64(selection_seed +
        static_cast<std::uint64_t>(round + 1) * UINT64_C(0x9e3779b97f4a7c15));
    int best = permuted_index(size, begin, seed);
    for (int logical = begin + 1; logical < end; ++logical) {
      const int candidate = permuted_index(size, logical, seed);
      if (scored[static_cast<std::size_t>(candidate)].fitness >
          scored[static_cast<std::size_t>(best)].fitness) best = candidate;
    }
    selected.push_back(static_cast<std::size_t>(best));
  }
  return selected;
}

// Reuse the GPU preparation's bounded candidate sets, including their order.
// Keep CPU tournament selection and ordinary post-crossover mutation unchanged.
std::vector<grammar::VariationSite> prepared_sites(
    const ProgramGenome& parent, const PreprocessOutput& prep, std::size_t index,
    grammar::VariationContext& context) {
  const auto analysis = context.analyze(parent);
  std::vector<grammar::VariationSite> sites;
  sites.reserve(prep.candidates.at(index).size());
  for (const auto& candidate : prep.candidates.at(index)) {
    const auto found = std::find_if(analysis->sites.begin(), analysis->sites.end(),
        [&](const grammar::VariationSite& site) {
          if (site.compatibility_id != candidate.compatibility_id ||
              site.occurrences.size() != static_cast<std::size_t>(candidate.occurrence_count))
            return false;
          for (std::size_t i = 0; i < site.occurrences.size(); ++i) {
            const auto& occurrence = prep.occurrences.at(candidate.occurrence_offset + i);
            if (site.occurrences[i].begin != static_cast<std::uint32_t>(occurrence.start) ||
                site.occurrences[i].end != static_cast<std::uint32_t>(occurrence.stop))
              return false;
          }
          return true;
        });
    if (found == analysis->sites.end())
      throw std::logic_error("prepared candidate has no compiled variation site");
    sites.push_back(*found);
  }
  return sites;
}

std::pair<ProgramGenome, ProgramGenome> candidate_crossover(
    const ProgramGenome& parent_a, const ProgramGenome& parent_b,
    const std::vector<grammar::VariationSite>& sites_a,
    const std::vector<grammar::VariationSite>& sites_b,
    std::uint64_t seed, grammar::VariationContext& context) {
  using namespace grammar;
  ++context.counters().crossover_attempts;
  auto a = variation_detail::certify(parent_a, context);
  auto b = variation_detail::certify(parent_b, context);
  const auto fits = [](const VariationSite& left, const VariationSite& right) {
    return donor_fits(left, right) && donor_fits(right, left);
  };
  std::uint64_t eligible = 0;
  for (const auto& left : sites_a) {
    for (const auto& right : sites_b) {
      if (!compatible_sites(left, right)) ++context.counters().contract_rejections;
      else if (!fits(left, right)) ++context.counters().budget_rejections;
      else ++eligible;
    }
  }
  if (eligible == 0)
    return {variation_detail::fallback(a, context), variation_detail::fallback(b, context)};
  // This is the GPU candidate-selection rank, not the CPU reservoir RNG.
  auto rank = hash64(seed ^ UINT64_C(0x517cc1b727220a95)) % eligible;
  for (const auto& left : sites_a) {
    for (const auto& right : sites_b) {
      if (!compatible_sites(left, right) || !fits(left, right)) continue;
      if (rank-- != 0) continue;
      auto child_a = variation_detail::splice(a.ast, left, b.ast,
          right.occurrences.front(), right.occurrence_binder_ids.front(), left.crossover_closed);
      auto child_b = variation_detail::splice(b.ast, right, a.ast,
          left.occurrences.front(), left.occurrence_binder_ids.front(), right.crossover_closed);
      return {variation_detail::accept(std::move(child_a), a, context),
              variation_detail::accept(std::move(child_b), b, context)};
    }
  }
  throw std::logic_error("compiled candidate selection lost its eligible pair");
}

ProgramGenome coupled_mutation(ProgramGenome child, const ProgramGenome& parent,
    const grammar::VariationSite* site, const PreprocessOutput& prep,
    const std::vector<grammar::VariationSite>& candidates, std::size_t parent_index,
    std::uint64_t seed, grammar::VariationContext& context, double subtree_probability) {
  using namespace grammar;
  // Retain the ablation's CPU branch draw. The donor is coupled to the original
  // crossover site, so subtree replacement starts from that original parent.
  std::mt19937_64 branch_rng(seed);
  if (site && !std::bernoulli_distribution(subtree_probability)(branch_rng))
    return mutate(child, seed, context, 0.0);
  ++context.counters().mutation_attempts;
  auto certified = variation_detail::certify(parent, context);
  if (!site) return variation_detail::fallback(certified, context);
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    const auto& prepared = candidates[i];
    if (prepared.compatibility_id != site->compatibility_id ||
        prepared.occurrences.size() != site->occurrences.size()) continue;
    bool same = true;
    for (std::size_t j = 0; j < site->occurrences.size(); ++j)
      same &= prepared.occurrences[j].begin == site->occurrences[j].begin &&
              prepared.occurrences[j].end == site->occurrences[j].end;
    if (!same) continue;
    const auto& candidate = prep.candidates.at(parent_index).at(i);
    if (candidate.donor_count == 0) return variation_detail::fallback(certified, context);
    const auto index = candidate.donor_offset + static_cast<int>(
        hash64(seed ^ UINT64_C(0xe7037ed1a0b428db)) % candidate.donor_count);
    const auto& donor = prep.donor_pool.at(index);
    const auto& contract = prep.donor_contracts.at(index);
    if (contract.compatibility_id != site->compatibility_id ||
        !donor_fits(*site, contract.materialized_nodes, contract.materialized_depth,
                    contract.template_nesting))
      throw std::logic_error("coupled donor no longer satisfies its prepared site");
    const std::vector<int> binders(
        prep.donor_binder_ids.begin() + contract.binder_offset,
        prep.donor_binder_ids.begin() + contract.binder_offset + contract.binder_count);
    return variation_detail::accept(variation_detail::splice(certified.ast, *site,
        donor.ast, {0, static_cast<std::uint32_t>(donor.ast.nodes.size())}, binders),
        certified, context);
  }
  throw std::logic_error("coupled crossover site is absent from donor preparation");
}

ReproductionResult run_cpu_backend_impl(const std::vector<ScoredGenomeRef>& scored,
                                        const EvolutionConfig& cfg,
                                        std::mt19937_64& rng) {
  if (scored.empty()) throw std::invalid_argument("compiled reproduction requires a nonempty population");
  ReproductionResult out;
  const auto prepare_t0 = std::chrono::steady_clock::now();
  grammar::VariationContext context(cfg.compiled_grammar, population_requests(cfg), 128,
      cfg.offspring_resource_budget);
  for (const auto& parent : scored) {
    if (!parent.genome) throw std::invalid_argument("compiled reproduction has a null parent");
    (void)context.analyze(*parent.genome);
  }
  std::vector<std::vector<grammar::VariationSite>> candidates;
  std::optional<PreprocessOutput> prepared;
  const bool coupled = cfg.cpu_repro_ablation == CpuReproAblation::GpuCoupledDonor;
  if (cfg.cpu_repro_ablation == CpuReproAblation::GpuCandidates || coupled) {
    std::vector<ProgramGenome> population;
    population.reserve(scored.size());
    std::uint64_t preparation_seed = cfg.seed ^ UINT64_C(1469598103934665603);
    for (const auto& parent : scored) {
      // Crossover certifies and compacts its parents. Prepare against that same
      // representation so unused name-table entries cannot change site scopes.
      population.push_back(grammar::variation_detail::certify(*parent.genome, context));
      for (unsigned char character : parent.genome->meta.program_key) {
        preparation_seed ^= character;
        preparation_seed *= UINT64_C(1099511628211);
      }
      preparation_seed = hash64(preparation_seed);
    }
    auto config = make_gpu_repro_config(population, cfg);
    config.seed = preparation_seed;
    if (coupled) {
      // CPU crossover can select any admitted site. Precompute contextual donors
      // for all of them; never substitute a newly sampled mutation site.
      for (const auto& parent : population) {
        const auto count = context.analyze(parent)->sites.size();
        if (count > 65536)
          throw std::invalid_argument("coupled donor preparation exceeds 65536 sites per parent");
        config.candidates_per_program = std::max(config.candidates_per_program,
                                                static_cast<int>(count));
      }
    }
    prepared.emplace(preprocess_population(population, config, context));
    const auto& prep = *prepared;
    candidates.reserve(population.size());
    for (std::size_t index = 0; index < population.size(); ++index)
      candidates.push_back(prepared_sites(population[index], prep, index, context));
    if (!coupled) prepared.reset();
  }
  out.stats.preprocess_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - prepare_t0).count();
  out.next_population.reserve(static_cast<std::size_t>(cfg.population_size));
  const int selected_count = ((cfg.population_size + 1) / 2) * 2;

  const auto selection_t0 = std::chrono::steady_clock::now();
  std::vector<std::size_t> selected;
  if (cfg.cpu_repro_ablation == CpuReproAblation::GpuSelection) {
    selected = gpu_style_selection_indices(scored, cfg.selection_pressure, selected_count, rng());
  } else {
    const bool odd_partner = cfg.population_size % 2 != 0 &&
        static_cast<std::size_t>(cfg.population_size) == scored.size();
    selected = tournament_selection_indices_without_replacement(
        scored, rng, cfg.selection_pressure, odd_partner ? cfg.population_size : selected_count);
    if (odd_partner) {
      const auto partner = tournament_selection_indices_without_replacement(
          scored, rng, cfg.selection_pressure, 1);
      selected.push_back(partner.front());
    }
    if (selected.size() > 1) std::shuffle(selected.begin(), selected.end(), rng);
  }
  out.stats.selection_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - selection_t0).count();

  std::uniform_real_distribution<double> probability(0.0, 1.0);
  std::uniform_int_distribution<std::uint64_t> seeds(0, UINT64_C(2000000000));
  for (std::size_t i = 0; i + 1 < selected.size() &&
       static_cast<int>(out.next_population.size()) < cfg.population_size; i += 2) {
    const auto& parent_a = *scored[selected[i]].genome;
    const auto& parent_b = *scored[selected[i + 1]].genome;
    const auto crossover_t0 = std::chrono::steady_clock::now();
    const auto crossover_seed = seeds(rng);
    grammar::variation_detail::SelectedCrossoverSites coupled_sites;
    auto children = cfg.cpu_repro_ablation == CpuReproAblation::GpuCandidates
        ? candidate_crossover(parent_a, parent_b, candidates.at(selected[i]),
            candidates.at(selected[i + 1]), crossover_seed, context)
        : coupled
            ? grammar::variation_detail::crossover_with_sites(
                parent_a, parent_b, crossover_seed, context, &coupled_sites)
            : crossover(parent_a, parent_b, crossover_seed, context);
    out.stats.crossover_ms += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - crossover_t0).count();
    const auto maybe_mutate = [&](ProgramGenome child, const ProgramGenome& parent,
                                  std::size_t parent_index, const grammar::VariationSite* site) {
      if (probability(rng) >= cfg.mutation_rate) return child;
      const auto mutation_t0 = std::chrono::steady_clock::now();
      const auto mutation_seed = seeds(rng);
      child = coupled
          ? coupled_mutation(std::move(child), parent, site, *prepared,
              candidates.at(parent_index), parent_index, mutation_seed, context,
              cfg.mutation_subtree_prob)
          : mutate(child, mutation_seed, context, cfg.mutation_subtree_prob);
      out.stats.mutation_ms += std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - mutation_t0).count();
      return child;
    };
    out.next_population.push_back(maybe_mutate(std::move(children.first), parent_a,
        selected[i], coupled_sites ? &coupled_sites->first : nullptr));
    if (static_cast<int>(out.next_population.size()) < cfg.population_size)
      out.next_population.push_back(maybe_mutate(std::move(children.second), parent_b,
          selected[i + 1], coupled_sites ? &coupled_sites->second : nullptr));
  }
  out.stats.variation = context.counters();
  return out;
}

}  // namespace

void require_reproduction_mode_supported(const EvolutionConfig& cfg, bool) {
  if (!cfg.compiled_grammar)
    throw std::invalid_argument("evolution requires a compiled grammar");
  if (!cfg.generation_request)
    throw std::invalid_argument("evolution requires an explicit generation request");
  if (cfg.cpu_repro_ablation != CpuReproAblation::None &&
      cfg.reproduction_backend != ReproductionBackend::Cpu)
    throw std::invalid_argument("CPU reproduction ablations require the CPU reproduction backend");
  if (cfg.fuel <= 0 || static_cast<std::uint32_t>(cfg.fuel) !=
      cfg.compiled_grammar->execution_limits().fuel)
    throw std::invalid_argument("compiled grammar execution fuel must match EvolutionConfig fuel");
  if (cfg.population_size <= 0)
    throw std::invalid_argument("compiled reproduction population_size must be positive");
  if (!std::isfinite(cfg.mutation_rate) || cfg.mutation_rate < 0 || cfg.mutation_rate > 1 ||
      !std::isfinite(cfg.mutation_subtree_prob) || cfg.mutation_subtree_prob < 0 ||
      cfg.mutation_subtree_prob > 1)
    throw std::invalid_argument("compiled reproduction mutation probabilities must be in [0,1]");
  if (cfg.additional_generation_requests.empty())
    (void)grammar::validate_request(*cfg.compiled_grammar, *cfg.generation_request);
  else
    grammar::validate_population_requests(*cfg.compiled_grammar, population_requests(cfg));
}

std::string reproduction_backend_name(ReproductionBackend backend) {
  return backend == ReproductionBackend::Gpu ? "gpu" : "cpu";
}

ReproductionBackend parse_reproduction_backend_name(const std::string& raw) {
  if (raw == "cpu") return ReproductionBackend::Cpu;
  if (raw == "gpu") return ReproductionBackend::Gpu;
  throw std::invalid_argument("unknown reproduction backend: " + raw);
}

std::string cpu_repro_ablation_name(CpuReproAblation ablation) {
  switch (ablation) {
    case CpuReproAblation::GpuSelection: return "gpu_selection";
    case CpuReproAblation::GpuCandidates: return "gpu_candidates";
    case CpuReproAblation::GpuCoupledDonor: return "gpu_coupled_donor";
    case CpuReproAblation::None: return "none";
  }
  throw std::invalid_argument("unknown cpu reproduction ablation");
}

CpuReproAblation parse_cpu_repro_ablation_name(const std::string& raw) {
  if (raw == "none") return CpuReproAblation::None;
  if (raw == "gpu_selection") return CpuReproAblation::GpuSelection;
  if (raw == "gpu_candidates") return CpuReproAblation::GpuCandidates;
  if (raw == "gpu_coupled_donor") return CpuReproAblation::GpuCoupledDonor;
  throw std::invalid_argument("unknown cpu reproduction ablation: " + raw);
}

ReproductionResult run_cpu_backend(const std::vector<ScoredGenome>& scored,
                                   const EvolutionConfig& cfg, std::mt19937_64& rng) {
  std::vector<ScoredGenomeRef> refs;
  refs.reserve(scored.size());
  for (const auto& one : scored) refs.push_back({&one.genome, one.fitness});
  return run_cpu_backend_impl(refs, cfg, rng);
}

ReproductionResult run_cpu_backend(const std::vector<ScoredGenomeRef>& scored,
                                   const EvolutionConfig& cfg, std::mt19937_64& rng) {
  return run_cpu_backend_impl(scored, cfg, rng);
}

ReproductionResult run_reproduction_backend(const std::vector<ScoredGenome>& scored,
                                            const EvolutionConfig& cfg,
                                            std::mt19937_64& rng) {
  require_reproduction_mode_supported(cfg);
  return cfg.reproduction_backend == ReproductionBackend::Gpu
      ? run_gpu_repro_backend(scored, cfg, rng) : run_cpu_backend(scored, cfg, rng);
}

ReproductionResult run_reproduction_backend(const std::vector<ScoredGenomeRef>& scored,
                                            const EvolutionConfig& cfg,
                                            std::mt19937_64& rng) {
  require_reproduction_mode_supported(cfg);
  return cfg.reproduction_backend == ReproductionBackend::Gpu
      ? run_gpu_repro_backend(scored, cfg, rng)
      : run_cpu_backend(scored, cfg, rng);
}

}  // namespace gagp::evo::repro
