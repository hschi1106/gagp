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
