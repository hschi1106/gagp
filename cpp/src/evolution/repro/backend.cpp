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

ReproductionResult run_cpu_backend_impl(const std::vector<ScoredGenomeRef>& scored,
                                        const EvolutionConfig& cfg,
                                        std::mt19937_64& rng) {
  if (scored.empty()) throw std::invalid_argument("compiled reproduction requires a nonempty population");
  ReproductionResult out;
  const auto prepare_t0 = std::chrono::steady_clock::now();
  grammar::VariationContext context(cfg.compiled_grammar, *cfg.generation_request);
  for (const auto& parent : scored) {
    if (!parent.genome) throw std::invalid_argument("compiled reproduction has a null parent");
    (void)context.cache().analyze(*parent.genome, context.request());
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
    auto children = crossover(parent_a, parent_b, seeds(rng), context);
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

void require_reproduction_mode_supported(const EvolutionConfig& cfg, bool) {
  if (!cfg.compiled_grammar)
    throw std::invalid_argument("evolution requires a compiled grammar");
  if (!cfg.generation_request)
    throw std::invalid_argument("evolution requires an explicit generation request");
  if (cfg.cpu_repro_ablation == CpuReproAblation::GpuCandidates ||
      cfg.cpu_repro_ablation == CpuReproAblation::GpuCoupledDonor)
    throw std::invalid_argument("compiled grammar reproduction does not support legacy donor ablations");
  if (cfg.fuel <= 0 || static_cast<std::uint32_t>(cfg.fuel) !=
      cfg.compiled_grammar->execution_limits().fuel)
    throw std::invalid_argument("compiled grammar execution fuel must match EvolutionConfig fuel");
  if (cfg.population_size <= 0)
    throw std::invalid_argument("compiled reproduction population_size must be positive");
  if (!std::isfinite(cfg.mutation_rate) || cfg.mutation_rate < 0 || cfg.mutation_rate > 1 ||
      !std::isfinite(cfg.mutation_subtree_prob) || cfg.mutation_subtree_prob < 0 ||
      cfg.mutation_subtree_prob > 1)
    throw std::invalid_argument("compiled reproduction mutation probabilities must be in [0,1]");
  (void)grammar::validate_request(*cfg.compiled_grammar, *cfg.generation_request);
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
