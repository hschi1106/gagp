#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "gagp/cli/json.hpp"
#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/search_policy.hpp"

namespace gagp::cli_detail {

// Parsed gagp-search-config-v1 document (spec/search_config.md). Every field
// is optional; an absent field keeps the caller's value. Unknown keys and
// out-of-range values are rejected. Only search semantics live here; engine
// choices (CPU/GPU, warp kernels, buffers) are never part of this document.
struct SearchConfig {
  std::optional<bool> phase_population;  // "representation"
  std::optional<int> population_size, generations, tournament_size, elite_count;
  std::optional<std::uint64_t> seed;
  std::optional<double> mutation_probability, penalty;
  std::optional<int> fuel;  // "execution.fuel"; may only lower the grammar fuel
  std::optional<int> site_depth;
  std::optional<bool> grammar_site_ranking;  // "ranking.tie_break"
  // Phase variation section. If any of these is present the whole policy is
  // explicit: absent members take the documented defaults, not legacy flags.
  std::optional<double> crossover_probability;
  std::optional<int> crossover_attempts;
  std::optional<int> mutation_max_nodes;
  std::optional<evo::PhaseSiteDraw> site_draw;
  std::optional<evo::PhaseSiteScope> site_scope;
  bool explicit_phase_variation() const;
};

inline constexpr const char* kSearchConfigFormat = "gagp-search-config-v1";

SearchConfig parse_search_config(const JsonValue& document);
SearchConfig load_search_config(const std::string& path);
// Applies present fields to `cfg`. Phase variation fields set cfg.phase_variation.
void apply_search_config(const SearchConfig& config, evo::EvolutionConfig& cfg);
// Effective search semantics of `cfg` as a complete gagp-search-config-v1
// document. A legacy-environment phase policy is resolved before encoding.
JsonValue effective_search_config(const evo::EvolutionConfig& cfg);

}  // namespace gagp::cli_detail
