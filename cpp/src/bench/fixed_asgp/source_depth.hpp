#pragma once

#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include "adapter.hpp"
#include "gagp/evolution/evolve.hpp"
#include "gp/operators.h"

namespace fixed_asgp {
inline std::optional<gg::ProjectedBudget> source_depth_budget(const gg::CompiledGrammar& grammar) {
  const auto* setting=std::getenv("GAGP_BM_SOURCE_DEPTH");
  if(!setting)return std::nullopt;
  if(std::string(setting)!="7")throw std::invalid_argument("GAGP_BM_SOURCE_DEPTH supports only exact source depth 7");
  const auto certified=grammar.resource_invariant_roots({grammar.entry()});
  if(certified.size()!=1 || !certified[0])
    throw std::invalid_argument("source depth requires canonical resource-invariant grammar");
  return gg::ProjectedBudget{grammar.search_limits().max_nodes,7};
}
inline void configure_source_depth(gagp::evo::EvolutionConfig& cfg) {
  if(cfg.compiled_grammar) {
    const auto budget=source_depth_budget(*cfg.compiled_grammar);
    if(budget && cfg.reproduction_backend==gagp::evo::repro::ReproductionBackend::Gpu &&
        !std::getenv("GAGP_GPU_PHASE_POPULATION"))
      throw std::invalid_argument("source depth GPU profile requires native GPU phase population");
    cfg.initial_resource_budget=budget;cfg.offspring_resource_budget=budget;
    cfg.gpu_projected_phase_profile=bool(budget) &&
        cfg.reproduction_backend==gagp::evo::repro::ReproductionBackend::Gpu;
  }
}
// Called only for a fresh prepared artifact under the explicitly selected
// profile. Runtime known-witness admission remains separate from search depth.
inline Json audit_source_depth(const asgp::Individual& source,
    const gagp::evo::ProgramGenome& native,const gg::CompiledGrammar& grammar,
    const asgp::TaskSpec& spec,const gg::ProjectedBudget& budget) {
  const auto witness=gg::reconstruct_derivation(grammar,native);
  if(!witness.resources || !budget.accepts(witness.resources->subtree(0)))
    throw std::invalid_argument("frozen parent exceeds projected source-depth search budget");
  const auto scheme=asgp::SchemeGrammar::Build(spec.Grammar());
  std::vector<Json> rows;
  for(int p=0;p<scheme.count;++p) {
    const std::string phase=spec.scheme==asgp::Scheme::kDynamicProgramming?
        (p==0?"Solve.":"Transition."):(p==0?"Solve.":p==1?"Divide.":"Combine.");
    const gg::DerivationChoice* root=nullptr;
    for(const auto& hole:witness.holes)for(const auto& choice:witness.choices) {
      if(choice.ast_begin==hole.ast_begin && choice.ast_end==hole.ast_end &&
          grammar.nonterminals().at(choice.nonterminal).stable_id.starts_with(phase)) {
        if(root && root->ast_begin!=choice.ast_begin)
          throw std::invalid_argument("source phase has multiple independent native roots");
        root=&choice;
      }
    }
    if(!root)throw std::invalid_argument("source phase missing native hole root");
    const auto source_height=asgp::SubtreeDepth(source.phases[p],0);
    const auto native_height=witness.resources->subtree(root->ast_begin).peak();
    if(source_height!=native_height || source_height>budget.max_depth)
      throw std::invalid_argument("native/source phase depth mismatch or source cap exceeded: phase="+std::to_string(p)+
          " source="+std::to_string(source_height)+" native="+std::to_string(native_height));
    rows.push_back(object({{"phase",number(p)},{"source_depth",number(source_height)},
        {"native_projected_depth",number(native_height)}}));
  }
  return array(std::move(rows));
}
} // namespace fixed_asgp
