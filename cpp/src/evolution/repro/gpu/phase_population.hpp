#pragma once
#include <memory>
#include <array>
#include <optional>
#include <vector>
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/grammar/compiled.hpp"
#include "gagp/evolution/repro/stats.hpp"
#include "gagp/evolution/search_policy.hpp"
namespace gagp { class FitnessSessionGpu; struct FitnessEvalResult; }
namespace gagp::evo::repro {
// Private run owner for the experimental native GPU genotype. Import performs
// complete admission; no mutable program, device pointer or certificate escapes.
// CPU code only transports/exports artifacts. Selection and variation run on GPU.
class NativePhasePopulation final {
 public:
  NativePhasePopulation(std::shared_ptr<const grammar::CompiledGrammar> grammar,
      const std::vector<ProgramGenome>& input, const std::vector<std::string>& input_names,
      std::optional<grammar::ProjectedBudget> projected=std::nullopt, int site_depth=0,
      const PhaseVariationPolicy& policy=PhaseVariationPolicy{});
  ~NativePhasePopulation();
  NativePhasePopulation(const NativePhasePopulation&) = delete;
  NativePhasePopulation& operator=(const NativePhasePopulation&) = delete;
  // Materialized reference view; callers must request it again after reproduction.
  const std::vector<BytecodeProgram>& programs() const;
  ReproductionStats reproduce(const std::vector<double>& completed_fitness,
      int tournament_size, double mutation_probability, std::uint64_t seed, int elite_count = 0);
  // Return independent member ASTs from the private construction invariant.
  // The optional canonical derivation cache may be absent; subsequent external
  // import/admission must validate these like any other supplied AST.
  std::vector<ProgramGenome> export_population() const;
  ProgramGenome export_member(std::size_t index) const;
  std::size_t device_bytes() const;
  std::size_t stage_storage_reserved_bytes() const;
  std::size_t stage_storage_peak_bytes() const;
  std::vector<unsigned> node_counts(bool grammar_sites=false) const;
  // Each row: independently mutable grammar roots, maximum per-phase site depth.
  std::vector<std::array<unsigned,2>> site_metrics() const;
  const char* profile_name() const;
  FitnessEvalResult evaluate(FitnessSessionGpu& session, bool diagnostics) const;

  // Synchronous batch steady state (search_config schedule.mode steady_state).
  // Children of one batch occupy offspring rows 0..batch-1: make_batch selects
  // parents from all current members and returns, per child, a current member
  // with an identical genotype (its result is reused) or -1; evaluate_batch
  // scores the other children; commit_batch copies {member, child} placements
  // into the population. After begin_steady the member programs are no longer
  // lowered, so evaluate() and programs() are rejected.
  void begin_steady(int batch);
  std::vector<int> make_batch(const std::vector<double>& selection_fitness, int tournament_size,
      double mutation_probability, std::uint64_t seed, ReproductionStats* timing = nullptr);
  FitnessEvalResult evaluate_batch(FitnessSessionGpu& session, const std::vector<int>& reuse,
      unsigned case_count, bool diagnostics, ReproductionStats* timing = nullptr);
  std::vector<unsigned> batch_nodes(bool grammar_sites = false) const;
  void commit_batch(const std::vector<std::array<int, 2>>& placements);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace gagp::evo::repro
