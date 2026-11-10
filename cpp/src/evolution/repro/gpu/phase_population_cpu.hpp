#pragma once
#include <array>
#include <memory>
#include <optional>
#include <vector>
#include "gagp/core/fitness_policy.hpp"
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/grammar/compiled.hpp"
#include "gagp/evolution/grammar/request.hpp"
#include "gagp/evolution/repro/stats.hpp"
#include "gagp/evolution/search_policy.hpp"
#include "gagp/runtime/cpu/fitness_cpu.hpp"

namespace gagp::evo::repro {
// Initial population of a site-depth phase run that starts without a saved
// population. One generated program fixes the shared skeleton; every phase gene
// is then grown by the phase generator under the owner's site-depth profile,
// ramped over invocation depths 2..site_depth (member i uses depth
// 2 + i mod (site_depth - 1)). The result depends only on the grammar, request,
// size, seed and site depth, so CPU and GPU owners start identically.
std::vector<ProgramGenome> generate_phase_initial_population(
    const grammar::CompiledGrammar& grammar, const grammar::GenerationRequest& request,
    const std::vector<std::string>& input_names, int population, std::uint64_t seed, int site_depth);

// CPU reference owner of the native phase population. It admits the same
// population as NativePhasePopulation and runs the identical per-child operator
// bodies (phase_operators.cuh) serially on the host, then lowers changed phases
// with the same phase compiler and evaluates the resulting bytecode with the
// CPU evaluator. For the same inputs, seed and policy it publishes the same
// genotypes and fitness as the GPU owner; it never touches a CUDA device.
class HostPhasePopulation final {
 public:
  HostPhasePopulation(std::shared_ptr<const grammar::CompiledGrammar> grammar,
      const std::vector<ProgramGenome>& input, const std::vector<std::string>& input_names,
      std::optional<grammar::ProjectedBudget> projected, int site_depth,
      const PhaseVariationPolicy& policy);
  ~HostPhasePopulation();
  HostPhasePopulation(const HostPhasePopulation&) = delete;
  HostPhasePopulation& operator=(const HostPhasePopulation&) = delete;

  struct Evaluation {
    std::vector<double> fitness;
    std::vector<unsigned> wrong;
    std::vector<CpuFitnessCaseCounts> case_counts;
    std::size_t evaluated_programs = 0, reused_programs = 0;
  };
  // Members unchanged from their parent (or identical to any parent) reuse the
  // parent's completed result; every other member is evaluated on all cases.
  Evaluation evaluate(const std::vector<CaseBindings>& cases, const std::vector<Value>& expected,
      int fuel, double penalty, int reduction_lanes, const FitnessPolicy& policy);
  // Generational step: the same selection, elites and draws as the GPU owner.
  ReproductionStats reproduce(const std::vector<double>& completed_fitness,
      int tournament_size, double mutation_probability, std::uint64_t seed, int elite_count = 0);
  // Synchronous batch steady state; the same contract as NativePhasePopulation.
  void begin_steady(int batch);
  std::vector<int> make_batch(const std::vector<double>& selection_fitness, int tournament_size,
      double mutation_probability, std::uint64_t seed, ReproductionStats* timing = nullptr);
  // Scores children whose reuse entry is -1; other entries stay zero.
  Evaluation evaluate_batch(const std::vector<int>& reuse, const std::vector<CaseBindings>& cases,
      const std::vector<Value>& expected, int fuel, double penalty, int reduction_lanes, const FitnessPolicy& policy);
  std::vector<unsigned> batch_nodes(bool grammar_sites = false) const;
  void commit_batch(const std::vector<std::array<int, 2>>& placements);
  std::vector<ProgramGenome> export_population() const;
  ProgramGenome export_member(std::size_t index) const;
  std::vector<unsigned> node_counts(bool grammar_sites = false) const;
  const char* profile_name() const;
  std::size_t host_bytes() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace gagp::evo::repro
