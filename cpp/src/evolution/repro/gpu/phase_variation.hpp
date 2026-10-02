#pragma once
#include "phase_grammar.hpp"
#include "device/phase_variation.cuh"

namespace gagp::evo::repro {
inline HostPhaseGrammar compile_phase_variation_grammar(
    const grammar::CompiledGrammar& grammar, unsigned root, unsigned max_depth) {
  auto profile = compile_phase_grammar(grammar, root, max_depth);
  const auto fail = [](const char* why) {
    throw std::invalid_argument(std::string("GPU phase variation: ") + why);
  };
  for (std::size_t i = 0; i < profile.nonterminals.size(); ++i) {
    if (!profile.nonterminals[i].production_count) continue;
    const auto& nt = grammar.nonterminals()[i];
    if (!nt.variation_enabled || nt.mutation_entry != grammar::kNoGrammarId)
      fail("variation policy requires native fallback");
    for (auto p : nt.productions) {
      const auto& production = grammar.productions()[p];
      if (!production.variation_enabled || production.closed_crossover ||
          !production.crossover_group.empty() || production.unbound_variation ||
          production.generation_mask != 3)
        fail("production policy requires native fallback");
    }
  }
  std::vector<bool> visited(grammar.expressions().size());
  std::function<void(unsigned)> visit = [&](unsigned id) {
    if (visited.at(id)) return;
    visited[id] = true;
    const auto& e = grammar.expressions()[id];
    if (e.resource_charge.nodes != 1 || e.resource_charge.depth != 1 || e.resource_charge.resets_depth)
      fail("projected resource charge requires native fallback");
    if (e.kind == grammar::ExpressionKind::Reference) {
      for (std::size_t j = 0; j < e.scope_mapping.size(); ++j)
        if (e.scope_mapping[j] != j) fail("nonidentity lexical mapping requires native fallback");
      for (auto p : grammar.nonterminals()[e.target].productions)
        visit(grammar.productions()[p].expression);
    }
    for (auto child : e.children) visit(child);
  };
  for (auto p : grammar.nonterminals()[root].productions)
    visit(grammar.productions()[p].expression);
  return profile;
}
} // namespace gagp::evo::repro
