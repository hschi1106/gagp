#pragma once
#include "phase_grammar.hpp"
#include "device/phase_variation.cuh"

namespace gagp::evo::repro {
inline HostPhaseGrammar compile_phase_variation_grammar(
    const grammar::CompiledGrammar& grammar, unsigned root, unsigned max_depth, unsigned site_depth=0,
    const std::vector<std::string>* inputs=nullptr) {
  auto profile = compile_phase_grammar(grammar, root, max_depth, site_depth, inputs);
  for (std::size_t i = 0; i < profile.nonterminals.size(); ++i) {
    if (!profile.nonterminals[i].production_count) continue;
    // A disabled nonterminal keeps its invocation sites out of variation.
    profile.nonterminals[i].fixed = !grammar.nonterminals()[i].variation_enabled;
  }
  return profile;
}
} // namespace gagp::evo::repro
