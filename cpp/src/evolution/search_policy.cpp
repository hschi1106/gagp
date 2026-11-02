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

PhaseSchedulePolicy resolve_phase_schedule(PhaseSchedulePolicy policy, int population) {
  if (policy.mode == PhaseScheduleMode::SteadyState && policy.batch == 0) policy.batch = population;
  return policy;
}

void validate_phase_schedule_policy(const PhaseSchedulePolicy& policy, int population) {
  const bool steady = policy.mode == PhaseScheduleMode::SteadyState;
  if (steady ? policy.batch < 0 : policy.batch != 0)
    throw std::invalid_argument("batch must be >= 1 (or omitted) for steady_state and absent otherwise");
  if (policy.replacement_size < 1 || policy.replacement_size > 64)
    throw std::invalid_argument("replacement size must be in [1,64]");
  if (!steady && policy.replacement_size != 2)
    throw std::invalid_argument("replacement size is a steady_state member");
  if (population < 1) return;
  if (steady && policy.batch && (policy.batch > population || population % policy.batch))
    throw std::invalid_argument("steady_state batch must divide the population");
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
