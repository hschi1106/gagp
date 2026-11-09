#pragma once

#include <string>

namespace gagp::evo {

// Search-semantic policy of the native phase-population operators. Every field
// changes the variation distribution or the random-draw sequence; engine
// choices (CPU reference or GPU, warp kernels, buffer strategies) must not.
// Defaults reproduce the historical reservoir/genome behavior.
enum class PhaseSiteDraw { Reservoir, Counted };
enum class PhaseSiteScope { Genome, UniformPhase };
struct PhaseVariationPolicy {
  // Reservoir draws one value per eligible site; Counted counts sites and draws
  // one rank. Both are uniform over sites but consume different draws.
  PhaseSiteDraw site_draw = PhaseSiteDraw::Reservoir;
  // Genome draws among all variable-phase sites; UniformPhase first draws one
  // variable phase, then a site inside it, for crossover and mutation.
  PhaseSiteScope site_scope = PhaseSiteScope::Genome;
  // Per-child crossover gate. Exactly 1 consumes no draw (historical default).
  double crossover_probability = 1.0;
  // Destination/donor site retries before the child keeps its parent phases.
  int crossover_attempts = 8;
  // Node cap of a freshly generated mutation subtree; 0 keeps only the phase
  // capacity. When the cap makes the site infeasible, the child is not mutated.
  int mutation_max_nodes = 0;

  bool operator==(const PhaseVariationPolicy& other) const {
    return site_draw == other.site_draw && site_scope == other.site_scope &&
           crossover_probability == other.crossover_probability &&
           crossover_attempts == other.crossover_attempts &&
           mutation_max_nodes == other.mutation_max_nodes;
  }
};

// Population schedule of the native phase population; both owners (GPU and
// CPU reference) implement both modes with identical results. The default is
// the generational loop.
//
// SteadyState is the synchronous batch steady state: each round
// selects parents from the whole current population, creates and scores
// `batch` children together, then places them one by one; each child replaces
// the loser of an inverse tournament of `replacement_size` members, never the
// population's best member. Rounds repeat until population * generations
// children were placed. `batch` 0 means the population size (one round per
// generation-equivalent).
enum class PhaseScheduleMode { Generational, SteadyState };
struct PhaseSchedulePolicy {
  PhaseScheduleMode mode = PhaseScheduleMode::Generational;
  int replacement_size = 2;
  int batch = 0;

  bool is_default() const { return mode == PhaseScheduleMode::Generational; }
  bool operator==(const PhaseSchedulePolicy& other) const {
    return mode == other.mode && replacement_size == other.replacement_size && batch == other.batch;
  }
};
// Throws std::invalid_argument for values that do not fit `population`
// (population < 1 skips population-dependent checks).
void validate_phase_schedule_policy(const PhaseSchedulePolicy& policy, int population = 0);
// The policy with an omitted steady-state batch (0) set to `population`.
PhaseSchedulePolicy resolve_phase_schedule(PhaseSchedulePolicy policy, int population);

// Throws std::invalid_argument for out-of-range values.
void validate_phase_variation_policy(const PhaseVariationPolicy& policy);
// Stable suffixes appended to reproduction profile names.
std::string phase_variation_profile_suffix(const PhaseVariationPolicy& policy);

}  // namespace gagp::evo
