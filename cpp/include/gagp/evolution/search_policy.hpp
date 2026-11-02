#pragma once

#include <string>

namespace gagp::evo {

// Search-semantic policy of the native phase-population operators. Every field
// changes the variation distribution or the random-draw sequence; engine
// choices (CPU reference or GPU, warp kernels, buffer strategies) must not.
// Defaults reproduce the historical behavior with no legacy flags set.
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

// Population schedule of the native phase population. Research semantics
// simulated by the CPU reference owner only; the GPU owner rejects any
// non-default schedule. The default is the generational loop, unchanged.
//
// Islands split the population into `islands` equal contiguous ranges; each
// range selects, keeps its elites and (async) replaces only inside itself.
// Every `migration_interval` generations (async: every interval*population
// completed children) the best `migrants` members of island i are copied to
// island (i+1) mod islands, replacing its last offspring slots (generational)
// or its worst members (async). Async replaces the generation barrier by a
// discrete-event simulation of `workers` concurrent children: a child is
// created from the population as it is at its start time, occupies its worker
// for `child_overhead` plus its largest per-case fuel (overhead only when its
// fitness is reused), and on completion replaces the loser of an inverse
// tournament of `replacement_size` members, never the island's best member.
//
// SteadyState (both owners) is the synchronous batch steady state: each round
// selects parents from the whole current population, creates and scores
// `batch` children together, then places them one by one; each child replaces
// the loser of an inverse tournament of `replacement_size` members, never the
// population's best member. Rounds repeat until population * generations
// children were placed. `batch` 0 means the population size (one round per
// generation-equivalent).
enum class PhaseScheduleMode { Generational, Async, SteadyState };
struct PhaseSchedulePolicy {
  PhaseScheduleMode mode = PhaseScheduleMode::Generational;
  int workers = 0;
  int replacement_size = 2;
  int child_overhead = 0;
  int islands = 1;
  int migration_interval = 0;
  int migrants = 0;
  int batch = 0;

  bool is_default() const {
    return mode == PhaseScheduleMode::Generational && islands == 1;
  }
  bool operator==(const PhaseSchedulePolicy& other) const {
    return mode == other.mode && workers == other.workers && replacement_size == other.replacement_size &&
           child_overhead == other.child_overhead && islands == other.islands &&
           migration_interval == other.migration_interval && migrants == other.migrants && batch == other.batch;
  }
};
// Throws std::invalid_argument for values that do not fit `population`
// (population < 1 skips population-dependent checks).
void validate_phase_schedule_policy(const PhaseSchedulePolicy& policy, int population = 0);
// The policy with an omitted steady-state batch (0) set to `population`.
PhaseSchedulePolicy resolve_phase_schedule(PhaseSchedulePolicy policy, int population);

// Throws std::invalid_argument for out-of-range values.
void validate_phase_variation_policy(const PhaseVariationPolicy& policy);
// Historical environment mapping: GAGP_COUNTED_SITE_DRAW (set),
// GAGP_GPU_UNIFORM_PHASE_SITES=1 and GAGP_GPU_SOURCE_CROSSOVER_GATE=1 (0.7).
PhaseVariationPolicy phase_variation_policy_from_environment();
// Explicit policies own these semantics. A legacy flag that is present and
// disagrees with the explicit value is rejected rather than silently ignored.
void require_environment_agrees(const PhaseVariationPolicy& policy);
// Stable suffixes appended to reproduction profile names.
std::string phase_variation_profile_suffix(const PhaseVariationPolicy& policy);

}  // namespace gagp::evo
