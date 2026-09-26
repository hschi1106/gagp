#include "gagp/evolution/grammar/request.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <set>

namespace gagp::evo::grammar {
void validate_population_requests(const CompiledGrammar& grammar,
    const std::vector<GenerationRequest>& requests) {
  if (requests.empty() || requests.size() > 8)
    throw std::invalid_argument("population requires between one and eight exact root requests");
  std::set<RType> types;
  for (const auto& request : requests) {
    (void)validate_request(grammar, request);
    grammar.require_executable(request.nonterminal);
    if (!types.insert(request.type).second)
      throw std::invalid_argument("population root requests must have distinct exact result types");
    if (requests.size() > 1 && !request.visible_environment.empty())
      throw std::invalid_argument("mixed population root requests must be lexically closed");
    if (request.budget.max_nodes != requests.front().budget.max_nodes ||
        request.budget.max_depth != requests.front().budget.max_depth)
      throw std::invalid_argument("population root requests must share node and depth budgets");
  }
}

GenerationRequest entry_request(const CompiledGrammar& grammar) {
  const auto& entry = grammar.nonterminals()[grammar.entry()];
  return {entry.id, entry.type, {}, grammar.search_limits()};
}

std::vector<GenerationRequest> named_population_requests(const CompiledGrammar& grammar,
    const std::vector<std::string>& root_names) {
  std::vector<GenerationRequest> requests;
  for (const auto& name : root_names) {
    const auto& roots = grammar.nonterminals();
    const auto found = std::find_if(roots.begin(), roots.end(),
        [&](const auto& root) { return root.stable_id == name; });
    if (found == roots.end()) throw std::invalid_argument("unknown population root: " + name);
    requests.push_back({found->id, found->type, {}, grammar.search_limits()});
  }
  validate_population_requests(grammar, requests);
  return requests;
}

std::vector<std::uint32_t> validate_request(const CompiledGrammar& grammar,
    const GenerationRequest& request) {
  (void)generation_stage_name(request.stage);
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
