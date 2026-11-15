#pragma once

#include <cstdint>
#include <functional>
#include <random>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gagp/evolution/case_set.hpp"
#include "gagp/core/fitness_policy.hpp"
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/grammar/request.hpp"
#include "gagp/evolution/search_policy.hpp"
#include "gagp/evolution/selection.hpp"
#include "gagp/evolution/timing.hpp"

namespace gagp::evo {

enum class EvalEngine {
  CPU,
  GPU,
};

struct EvolutionConfig {
  int population_size = 64;
  int generations = 40;
  double mutation_rate = 0.5;
  double penalty = 1.0;
  FitnessPolicy fitness_policy;
  int elite_count = 0;
  // GPU runs the resident GPU owner (GPU evaluation and reproduction); CPU runs
  // the CPU reference owner with the same operators and draws.
  EvalEngine eval_engine = EvalEngine::CPU;
  int gpu_blocksize = 1024;
  int selection_pressure = 2;
  std::uint64_t seed = 0;
  int fuel = 20000;
  // Immutable compiled search space; programs derive from its entry.
  std::shared_ptr<const grammar::CompiledGrammar> compiled_grammar;
  // Phase-population site policy: depth counts grammar invocations, excluding
  // lowering scaffolds; zero disables it.
  int gpu_phase_site_depth = 0;
  bool gpu_phase_site_parsimony = false;
  // Phase variation semantics; unset uses the PhaseVariationPolicy defaults.
  std::optional<PhaseVariationPolicy> phase_variation;
  // Population schedule (generational or synchronous batch steady state).
  // Unset keeps the generational loop.
  std::optional<PhaseSchedulePolicy> phase_schedule;
  bool skip_final_eval = false;
  bool retain_final_population = true;
};

struct EvolutionResult {
  ScoredGenome best;
  std::vector<ScoredGenome> history_best;
  std::vector<double> history_best_fitness;
  std::vector<double> history_mean_fitness;
  std::vector<ScoredGenome> final_population;
  bool final_eval_skipped = false;
  std::string reproduction_profile;
  std::uint64_t genotype_device_bytes = 0;
  std::uint64_t gpu_stage_storage_reserved_bytes = 0;
  std::uint64_t gpu_stage_storage_peak_bytes = 0;
  EvolutionTiming timing;
};

// Runs the resident phase population: the GPU owner for EvalEngine::GPU, the
// CPU reference owner for EvalEngine::CPU. A grammar or initial genome the
// resident engine cannot admit throws std::invalid_argument.
EvolutionResult evolve_population(const std::vector<EvalCase>& cases,
                                  const EvolutionConfig& cfg,
                                  const std::vector<ProgramGenome>* initial_population = nullptr);

}  // namespace gagp::evo
