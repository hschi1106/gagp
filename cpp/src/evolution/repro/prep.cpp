#include "gagp/evolution/repro/prep.hpp"

#include <algorithm>
#include <stdexcept>

#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/grammar/request.hpp"

namespace gagp::evo::repro {

GpuReproConfig make_gpu_repro_config(const std::vector<ProgramGenome>& population,
                                     const EvolutionConfig& cfg) {
  if (!cfg.compiled_grammar || !cfg.generation_request)
    throw std::invalid_argument("compiled preparation requires a grammar and request");
  if (population.empty() || population.size() > 65536)
    throw std::invalid_argument("compiled preparation population size must be in [1,65536]");
  const auto& request = *cfg.generation_request;
  (void)grammar::validate_request(*cfg.compiled_grammar, request);
  if (!cfg.additional_generation_requests.empty())
    grammar::validate_population_requests(*cfg.compiled_grammar, population_requests(cfg));
  GpuReproConfig out;
  out.population_size = static_cast<int>(population.size());
  out.pair_count = (out.population_size + 1) / 2;
  out.max_nodes = static_cast<int>(request.budget.max_nodes);
  out.max_expr_depth = static_cast<int>(request.budget.max_depth);
  out.max_donor_nodes = 1;
  out.max_names = 1;
  out.max_consts = 1;
  out.tournament_k = std::max(1, std::min(out.population_size, cfg.selection_pressure));
  out.mutation_ratio = cfg.mutation_rate;
  out.mutation_subtree_ratio = cfg.mutation_subtree_prob;
  out.seed = cfg.seed;
  for (const auto& genome : population) {
    if (genome.ast.names.size() > 65536 || genome.ast.consts.size() > 65536)
      throw std::invalid_argument("compiled preparation requires compact bounded tables");
    out.max_names = std::max(out.max_names, static_cast<int>(genome.ast.names.size()));
    out.max_consts = std::max(out.max_consts, static_cast<int>(genome.ast.consts.size()));
  }
  return out;
}

}  // namespace gagp::evo::repro
