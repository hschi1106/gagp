#pragma once
#include <array>
#include <memory>
#include <optional>
#include <vector>
#include "gagp/core/fitness_policy.hpp"
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/grammar/compiled.hpp"
#include "gagp/evolution/repro/stats.hpp"
#include "gagp/evolution/search_policy.hpp"
#include "gagp/runtime/cpu/fitness_cpu.hpp"

namespace gagp::evo::repro {
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
      const PhaseVariationPolicy& policy, const PhaseSchedulePolicy& schedule = {});
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
  // Generational step. With islands, selection, elites and draws stay inside
  // each island and scheduled migrants replace the last offspring slots.
  ReproductionStats reproduce(const std::vector<double>& completed_fitness,
      int tournament_size, double mutation_probability, std::uint64_t seed, int elite_count = 0);
  // Fuel metered by the latest evaluate(), per member (research schedule traces).
  struct MemberFuel { std::uint64_t sum = 0; std::uint32_t max = 0; bool evaluated = false; };
  const std::vector<MemberFuel>& last_fuel() const;

  // Async schedule: one in-flight child per worker. start_child creates the
  // child of `worker` from members [first, first+count) as they are now and
  // returns a current member with an identical genotype (its result is
  // reused) or -1; evaluate_child scores it; commit_child makes it member
  // `victim`. copy_member migrates a member unchanged.
  struct ChildEvaluation {
    double fitness = 0; unsigned wrong = 0; CpuFitnessCaseCounts counts;
    std::uint64_t fuel_sum = 0; std::uint32_t fuel_max = 0;
  };
  void begin_async(int workers);
  int start_child(int worker, const std::vector<double>& selection_fitness, int first, int count,
      int tournament_size, double mutation_probability, std::uint64_t seed);
  ChildEvaluation evaluate_child(int worker, const std::vector<CaseBindings>& cases, const std::vector<Value>& expected,
      int fuel, double penalty, int reduction_lanes, const FitnessPolicy& policy);
  unsigned child_nodes(int worker, bool grammar_sites) const;
  void commit_child(int worker, int victim);
  void copy_member(int source, int target);

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
