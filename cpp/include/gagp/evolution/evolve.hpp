#pragma once

#include <cstdint>
#include <functional>
#include <random>
#include <memory>
#include <optional>
#include <vector>

#include "gagp/evolution/case_set.hpp"
#include "gagp/core/fitness_policy.hpp"
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/grammar/request.hpp"
#include "gagp/evolution/input_spec.hpp"
#include "gagp/evolution/repro/backend.hpp"
#include "gagp/evolution/selection.hpp"
#include "gagp/evolution/timing.hpp"

namespace gagp::evo {

enum class EvalEngine {
  CPU,
  GPU,
};

// Bounded CPU reference scoring of the current GPU leaders on the training set.
// Disabled by default. It does not certify unverified candidates or replace GPU
// reproduction. Scheduled generations and the final evaluation verify top_k;
// near_zero_error >= 0 also triggers when the leading loss is within that value.
struct CpuFitnessVerification {
  int top_k = 0;
  int interval = 1;
  double near_zero_error = -1.0;
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
  repro::CpuReproAblation cpu_repro_ablation = repro::CpuReproAblation::None;
  bool repro_overlap = false;
  int gpu_blocksize = 1024;
  int selection_pressure = 2;
  std::uint64_t seed = 0;
  int fuel = 20000;
  // Own the immutable compiled search space across worker copies.
  std::shared_ptr<const grammar::CompiledGrammar> compiled_grammar;
  std::optional<grammar::GenerationRequest> generation_request;
  // Optional exact roots in the same grammar; selection remains population-wide.
  // Empty retains the single-request generation and replay contract.
  std::vector<grammar::GenerationRequest> additional_generation_requests;
  // Optional authored-cost admission for offspring, independent of physical
  // allocation limits and initial-population admission.
  std::optional<grammar::ProjectedBudget> offspring_resource_budget;
  // Separate initial admission permits source profiles whose construction and
  // variation use different depth rules. No option changes physical capacities.
  std::optional<grammar::ProjectedBudget> initial_resource_budget;
  // Explicit strict native GPU projected-phase profile. Generic resource budgets
  // alone retain ordinary backend fallback. Requires GPU eval/reproduction,
  // GAGP_GPU_PHASE_POPULATION, and matching supported initial/offspring budgets.
  bool gpu_projected_phase_profile = false;

  // Optional private GPU operator policy: depth counts grammar invocations,
  // excluding lowering scaffolds. Independent of physical AST admission.
  // Zero disables it. Explicit requests fail rather than changing backend.
  int gpu_phase_site_depth = 0;
  bool gpu_phase_site_parsimony = false;
  CpuFitnessVerification cpu_fitness_verification;
  // Diagnostic snapshots, in original population order, after scoring and before
  // reproduction. The callback must not retain payload handles past its call.
  // Generation G is the final evaluation; it is invalid with skip_final_eval.
  std::vector<int> checkpoint_generations;
  std::function<void(int, const std::vector<ScoredGenome>&)> evaluation_checkpoint;

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
  std::string reproduction_fallback_reason;
  std::uint64_t genotype_device_bytes = 0;
  std::uint64_t gpu_stage_storage_reserved_bytes = 0;
  std::uint64_t gpu_stage_storage_peak_bytes = 0;
  EvolutionTiming timing;
};

std::vector<ScoredGenome> evaluate_population(const std::vector<ProgramGenome>& population,
                                              const std::vector<EvalCase>& cases,
                                              const EvolutionConfig& cfg);
EvolutionResult evolve_population(const std::vector<EvalCase>& cases,
                                  const EvolutionConfig& cfg,
                                  const std::vector<ProgramGenome>* initial_population = nullptr);

std::string eval_engine_name(EvalEngine engine);

}  // namespace gagp::evo
