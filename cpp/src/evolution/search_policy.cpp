#include "gagp/evolution/search_policy.hpp"

#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace gagp::evo {
namespace {
bool flag_is_one(const char* name) {
  const char* value = std::getenv(name);
  return value && std::string(value) == "1";
}
}  // namespace

void validate_phase_variation_policy(const PhaseVariationPolicy& policy) {
  if (!std::isfinite(policy.crossover_probability) || policy.crossover_probability < 0 ||
      policy.crossover_probability > 1)
    throw std::invalid_argument("crossover probability must be in [0,1]");
  if (policy.crossover_attempts < 1 || policy.crossover_attempts > 64)
    throw std::invalid_argument("crossover attempts must be in [1,64]");
  if (policy.mutation_max_nodes < 0 || policy.mutation_max_nodes > 1024)
    throw std::invalid_argument("mutation max subtree nodes must be 0 (off) or in [1,1024]");
}

void validate_phase_schedule_policy(const PhaseSchedulePolicy& policy, int population) {
  const bool async = policy.mode == PhaseScheduleMode::Async;
  const bool steady = policy.mode == PhaseScheduleMode::SteadyState;
  if (steady ? policy.batch < 1 : policy.batch != 0)
    throw std::invalid_argument("batch must be >= 1 for steady_state and absent otherwise");
  if (steady && (policy.islands != 1 || policy.workers || policy.child_overhead))
    throw std::invalid_argument("steady_state takes only batch and replacement_size");
  if (policy.islands < 1 || policy.islands > 1024) throw std::invalid_argument("islands must be in [1,1024]");
  if (policy.migration_interval < 0 || policy.migrants < 0)
    throw std::invalid_argument("migration interval and migrants must be nonnegative");
  if ((policy.migration_interval == 0) != (policy.migrants == 0) || (policy.islands == 1 && policy.migrants))
    throw std::invalid_argument("migration needs islands > 1 and both an interval and migrants");
  if (async ? policy.workers < 1 : policy.workers != 0)
    throw std::invalid_argument("workers must be >= 1 for async and absent otherwise");
  if (policy.replacement_size < 1 || policy.replacement_size > 64)
    throw std::invalid_argument("replacement size must be in [1,64]");
  if (policy.child_overhead < 0) throw std::invalid_argument("child overhead must be nonnegative");
  if (!async && policy.child_overhead != 0) throw std::invalid_argument("child overhead is an async member");
  if (!async && !steady && policy.replacement_size != 2)
    throw std::invalid_argument("replacement size is an async or steady_state member");
  if (population < 1) return;
  if (steady && (policy.batch > population || population % policy.batch))
    throw std::invalid_argument("steady_state batch must divide the population");
  if (population % policy.islands) throw std::invalid_argument("islands must divide the population");
  const int island = population / policy.islands;
  if (policy.islands > 1 && island < 2) throw std::invalid_argument("islands need at least two members");
  // Sources (an island's best) and targets (its worst) never overlap.
  if (2 * policy.migrants > island) throw std::invalid_argument("migrants must be at most half the island size");
  if (async && (policy.workers > population || policy.workers % policy.islands))
    throw std::invalid_argument("async workers must be <= population and divisible by islands");
}

PhaseVariationPolicy phase_variation_policy_from_environment() {
  PhaseVariationPolicy policy;
  if (std::getenv("GAGP_COUNTED_SITE_DRAW")) policy.site_draw = PhaseSiteDraw::Counted;
  if (flag_is_one("GAGP_GPU_UNIFORM_PHASE_SITES")) policy.site_scope = PhaseSiteScope::UniformPhase;
  if (flag_is_one("GAGP_GPU_SOURCE_CROSSOVER_GATE")) policy.crossover_probability = 0.7;
  return policy;
}

void require_environment_agrees(const PhaseVariationPolicy& policy) {
  if (std::getenv("GAGP_COUNTED_SITE_DRAW") && policy.site_draw != PhaseSiteDraw::Counted)
    throw std::invalid_argument("GAGP_COUNTED_SITE_DRAW conflicts with the explicit site draw policy");
  if (const char* value = std::getenv("GAGP_GPU_UNIFORM_PHASE_SITES"))
    if ((std::string(value) == "1") != (policy.site_scope == PhaseSiteScope::UniformPhase))
      throw std::invalid_argument("GAGP_GPU_UNIFORM_PHASE_SITES conflicts with the explicit site scope");
  if (const char* value = std::getenv("GAGP_GPU_SOURCE_CROSSOVER_GATE"))
    if ((std::string(value) == "1") != (policy.crossover_probability == 0.7))
      throw std::invalid_argument("GAGP_GPU_SOURCE_CROSSOVER_GATE conflicts with the explicit crossover probability");
}

std::string phase_variation_profile_suffix(const PhaseVariationPolicy& policy) {
  std::ostringstream out;
  if (policy.crossover_probability == 0.7) out << "-source-cross07";
  else if (policy.crossover_probability != 1) out << "-cross" << policy.crossover_probability;
  if (policy.crossover_attempts != 8) out << "-attempts" << policy.crossover_attempts;
  if (policy.mutation_max_nodes) out << "-mutcap" << policy.mutation_max_nodes;
  return out.str();
}

}  // namespace gagp::evo
