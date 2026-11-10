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
#include "gagp/evolution/input_spec.hpp"
#include "gagp/evolution/repro/backend.hpp"
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
  double mutation_subtree_prob = 0.8;
  double penalty = 1.0;
  FitnessPolicy fitness_policy;
  int elite_count = 0;
  EvalEngine eval_engine = EvalEngine::CPU;
  repro::ReproductionBackend reproduction_backend = repro::ReproductionBackend::Cpu;
  int gpu_blocksize = 1024;
  int selection_pressure = 2;
  std::uint64_t seed = 0;
  int fuel = 20000;
  // Own the immutable compiled search space across worker copies.
  std::shared_ptr<const grammar::CompiledGrammar> compiled_grammar;
  std::optional<grammar::GenerationRequest> generation_request;
  // Optional private GPU operator policy: depth counts grammar invocations,
  // excluding lowering scaffolds. Independent of physical AST admission.
  // Zero disables it. Explicit requests fail rather than changing backend.
  int gpu_phase_site_depth = 0;
  bool gpu_phase_site_parsimony = false;
  // Native phase-population representation and operators. Unset keeps the
  // legacy GPU-only GAGP_GPU_PHASE_POPULATION switch. True selects it for
  // matching engines: GPU/GPU runs the resident GPU owner, CPU/CPU the CPU
  // reference owner with the same operators and draws. False disables it.
  std::optional<bool> phase_population;
  // Explicit phase variation semantics. Unset reads the legacy environment
  // flags; when set, a conflicting legacy flag is rejected.
  std::optional<PhaseVariationPolicy> phase_variation;
  // Population schedule of the phase population (generational or synchronous
  // batch steady state). Unset keeps the generational loop.
  std::optional<PhaseSchedulePolicy> phase_schedule;
  bool skip_final_eval = false;
  bool retain_final_population = true;
  // Derived by evolve_population for verifier checks at reproduction boundaries.
  std::vector<InputSpec> verification_inputs;
};

std::vector<grammar::GenerationRequest> population_requests(const EvolutionConfig& config);

struct EvolutionResult {
  ScoredGenome best;
  std::vector<ScoredGenome> history_best;
  std::vector<double> history_best_fitness;
  std::vector<double> history_mean_fitness;
  std::vector<ScoredGenome> final_population;
  bool final_eval_skipped = false;
  std::string reproduction_profile = "native-ast";
  // Non-empty when the resident engine declined the grammar or a genome at
  // admission and the search ran on the native representation instead.
  std::string resident_declined;
  std::uint64_t genotype_device_bytes = 0;
  std::uint64_t gpu_stage_storage_reserved_bytes = 0;
  std::uint64_t gpu_stage_storage_peak_bytes = 0;
  EvolutionTiming timing;
};

std::vector<ScoredGenome> evaluate_population(const std::vector<ProgramGenome>& population,
                                              const std::vector<EvalCase>& cases,
                                              const EvolutionConfig& cfg);
// Runs the resident engine when requested; if it declines at admission, the
// same search reruns with native_fallback_config(cfg).
EvolutionResult evolve_population(const std::vector<EvalCase>& cases,
                                  const EvolutionConfig& cfg,
                                  const std::vector<ProgramGenome>* initial_population = nullptr);
// The native-representation config used after a resident decline: phase-only
// policies cleared, host reproduction, unchanged evaluation engine.
EvolutionConfig native_fallback_config(const EvolutionConfig& cfg);

std::string eval_engine_name(EvalEngine engine);

}  // namespace gagp::evo
