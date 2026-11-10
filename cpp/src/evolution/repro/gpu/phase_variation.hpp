#pragma once
#include "phase_grammar.hpp"
#include "device/phase_variation.cuh"

namespace gagp::evo::repro {
inline HostPhaseGrammar compile_phase_variation_grammar(
    const grammar::CompiledGrammar& grammar, unsigned root, unsigned max_depth, unsigned logical_depth=0, bool site_depth=false,
    const std::vector<std::string>* inputs=nullptr) {
  auto profile = compile_phase_grammar(grammar, root, max_depth, logical_depth, site_depth, inputs);
  const auto fail = [](const char* why) {
    throw std::invalid_argument(std::string("GPU phase variation: ") + why);
  };
  for (std::size_t i = 0; i < profile.nonterminals.size(); ++i) {
    if (!profile.nonterminals[i].production_count) continue;
    // A disabled nonterminal keeps its invocation sites out of variation.
    profile.nonterminals[i].fixed = !grammar.nonterminals()[i].variation_enabled;
  }
  std::vector<bool> visited(grammar.expressions().size());
  std::function<void(unsigned)> visit = [&](unsigned id) {
    if (visited.at(id)) return;
    visited[id] = true;
    const auto& e = grammar.expressions()[id];
    // Physical and site-depth budgets never read authored resource charges.
    if (logical_depth && !site_depth && (e.resource_charge.nodes != 1 || e.resource_charge.depth > 1 || e.resource_charge.resets_depth))
      fail("projected resource charge requires native fallback");
    if (e.kind == grammar::ExpressionKind::Reference) {
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
