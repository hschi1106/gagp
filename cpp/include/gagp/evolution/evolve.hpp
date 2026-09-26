#pragma once

#include <cstdint>
#include <random>
#include <memory>
#include <optional>
#include <vector>

#include "gagp/evolution/case_set.hpp"
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

struct EvolutionConfig {
  int population_size = 64;
  int generations = 40;
  double mutation_rate = 0.5;
  double mutation_subtree_prob = 0.8;
  double penalty = 1.0;
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
