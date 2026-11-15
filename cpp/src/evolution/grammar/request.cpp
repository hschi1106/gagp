#include "gagp/evolution/grammar/request.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>

namespace gagp::evo::grammar {
GenerationRequest entry_request(const CompiledGrammar& grammar) {
  const auto& entry = grammar.nonterminals()[grammar.entry()];
  return {entry.id, entry.type, {}, grammar.search_limits()};
}

std::vector<std::uint32_t> validate_request(const CompiledGrammar& grammar,
    const GenerationRequest& request) {
  if (request.nonterminal >= grammar.nonterminals().size())
    throw std::invalid_argument("generation request has an unknown nonterminal ID");
  const auto& target = grammar.nonterminals()[request.nonterminal];
  if (request.type != target.type)
    throw std::invalid_argument("generation request exact type differs from nonterminal " + target.stable_id);
  if (target.category != NodeCategory::Expression && target.category != NodeCategory::Program)
    throw std::invalid_argument("generation request requires an Expression or Program nonterminal for a complete native AST");
  if (!request.budget.max_nodes || !request.budget.max_depth ||
      request.budget.max_nodes > grammar.search_limits().max_nodes ||
      request.budget.max_depth > grammar.search_limits().max_depth)
    throw std::invalid_argument("generation request budget must be positive and within the compiled grammar limits");
  if (request.visible_environment.size() > 4096)
    throw std::invalid_argument("generation request exceeds 4096 visible lexical bindings");
  std::map<std::string, std::uint32_t> bindings;
  const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
  for (std::size_t i = 0; i < request.visible_environment.size(); ++i) {
    const auto& binding = request.visible_environment[i];
    if (binding.name.empty() || !letter(binding.name.front()) ||
        !std::all_of(binding.name.begin(), binding.name.end(), [&](char c) { return letter(c) || (c >= '0' && c <= '9'); }) ||
        !bindings.emplace(binding.name, static_cast<std::uint32_t>(i)).second)
      throw std::invalid_argument("generation request has an invalid or duplicate lexical binding name");
    (void)type_name(binding.type);
  }
  std::vector<std::uint32_t> mapping;
  for (const auto& required : target.scope) {
    const auto found = bindings.find(required.name);
    if (found == bindings.end() || request.visible_environment[found->second].type != required.type)
      throw std::invalid_argument("generation request requires visible binding " + required.name + " with exact type " + std::string(type_name(required.type)));
    mapping.push_back(found->second);
  }
  const bool wrap = target.category == NodeCategory::Expression;
  if (wrap && (request.budget.max_nodes <= 4 || request.budget.max_depth <= 3))
    throw std::invalid_argument("generation request cannot fit the four-node, three-level expression envelope");
  const auto nodes = request.budget.max_nodes - (wrap ? 4 : 0);
  const auto depth = request.budget.max_depth - (wrap ? 3 : 0);
  if (target.minimum_nodes_by_depth.at(depth) > nodes)
    throw std::invalid_argument("generation request has no feasible derivation for " + target.stable_id + " within its remaining node/depth budget");
  return mapping;
}
}  // namespace gagp::evo::grammar
