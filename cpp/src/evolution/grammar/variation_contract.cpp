#include "gagp/evolution/grammar/variation_contract.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>

namespace gagp::evo::grammar {
namespace {
void field(std::string& key, const std::string& value) {
  key += std::to_string(value.size()) + ":" + value;
}
void number(std::string& key, std::uint32_t value) { field(key, std::to_string(value)); }
void bindings(std::string& key, const std::vector<RegionBinding>& values) {
  number(key, static_cast<std::uint32_t>(values.size()));
  for (const auto& value : values) {
    field(key, value.name); number(key, static_cast<std::uint32_t>(value.type));
  }
}
std::string contract_key(const CompiledGrammar& grammar, const VariationSite& site) {
  std::string key;
  field(key, "grammar-variation-contract-v1");
  field(key, grammar.content_hash());
  field(key, kGrammarSemanticVersion);
  number(key, site.nonterminal); number(key, static_cast<std::uint32_t>(site.type));
  number(key, static_cast<std::uint32_t>(site.category)); number(key, site.context);
  number(key, site.template_id); number(key, site.slot);
  bindings(key, site.visible_environment); bindings(key, site.available_locals);
  return key;
}
std::vector<RegionBinding> native_scope(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const VerifiedAst& verified, std::uint32_t index, NodeCategory category) {
  // A complete Program is evaluated from its declared input environment.
  if (category == NodeCategory::Program) return grammar.inputs();
  const auto id = verified.expression_scope_ids.at(index);
  if (id == kNoGrammarId) throw std::logic_error("variation expression lacks an exact native scope");
  const auto& scope = verified.scopes.at(id);

  std::vector<RegionBinding> result;
  for (const auto& binding : scope.locals)
    result.push_back({genome.ast.names.at(binding.first), binding.second});
  std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
  return result;
}
void intersect(std::vector<RegionBinding>& left, const std::vector<RegionBinding>& right) {
  left.erase(std::remove_if(left.begin(), left.end(), [&](const auto& binding) {
    return std::none_of(right.begin(), right.end(), [&](const auto& other) {
      return binding.name == other.name && binding.type == other.type;
    });
  }), left.end());
}
}  // namespace

std::uint32_t CompatibilityRegistry::intern(const std::string& key) {
  if (key.empty()) throw std::invalid_argument("cannot intern an empty grammar compatibility contract");
  const auto found = ids_.find(key);
  if (found != ids_.end()) return found->second;
  if (keys_.size() >= 65536) throw std::invalid_argument("grammar compatibility registry exceeds 65536 contracts");
  const auto id = static_cast<std::uint32_t>(keys_.size());
  keys_.push_back(key); ids_.emplace(key, id);
  return id;
}

VariationAnalysis analyze_variation(const CompiledGrammar& grammar, const ProgramGenome& genome,
    CompatibilityRegistry* registry) {
  return analyze_variation(grammar, genome, entry_request(grammar), registry);
}

VariationAnalysis analyze_variation(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const GenerationRequest& request, CompatibilityRegistry* registry) {
  VariationAnalysis result;
  std::vector<std::vector<int>> choice_environments;
  result.witness = reconstruct_derivation(grammar, genome, request, &result.verified, &choice_environments);
  const auto& witness = result.witness;
  const auto& verified = result.verified;
  std::vector<std::uint32_t> depths(genome.ast.nodes.size());
  std::vector<std::size_t> ends;
  for (std::size_t index = 0; index < depths.size(); ++index) {
    while (!ends.empty() && index >= ends.back()) ends.pop_back();
    depths[index] = static_cast<std::uint32_t>(ends.size() + 1);
    ends.push_back(verified.subtree_end[index]);
  }
  using Group = std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t>;
  std::map<Group, std::size_t> groups;
  for (std::size_t choice_index = 0; choice_index < witness.choices.size(); ++choice_index) {
    const auto& choice = witness.choices[choice_index];
    const auto& nt = grammar.nonterminals().at(choice.nonterminal);
    // Complete Program and expression donors share the existing generation request
    // contract. Structural Block/Stmt fragments need their own contextual lowering.
    if (nt.category != NodeCategory::Expression && nt.category != NodeCategory::Program) continue;
    const auto& environment = choice_environments.at(choice_index);
    if (environment.size() != nt.scope.size())
      throw std::logic_error("variation witness lexical scope arity differs from its nonterminal");
    if (!environment.empty()) {
      const auto& scope = verified.scopes.at(verified.expression_scope_ids.at(choice.ast_begin));
      for (std::size_t i = 0; i < environment.size(); ++i) {
        if (environment[i] < 0) continue;  // Unconsumed external request binding.
        const auto binding = std::make_pair(-environment[i] - 1, nt.scope[i].type);
        if (std::find(scope.binders.begin(), scope.binders.end(), binding) == scope.binders.end())
          throw std::logic_error("variation witness lexical binding is unavailable at its occurrence");
      }
    }
    const auto logical = witness.nodes.at(choice.ast_begin).logical_instance;
    const Group group{choice.nonterminal, logical, choice.template_instance, choice.slot};
    auto found = groups.find(group);
    if (found == groups.end()) {
      VariationSite site;
      site.nonterminal = choice.nonterminal; site.type = nt.type; site.category = nt.category;
      site.context = nt.context; site.slot = choice.slot;
      if (choice.template_instance != kNoGrammarId)
        site.template_id = witness.templates.at(choice.template_instance).template_id;
      const auto& ids = choice_environments.at(choice_index);
      const bool materialized_scope = !ids.empty() &&
          std::all_of(ids.begin(), ids.end(), [](int id) { return id >= 0; });
      site.visible_environment = materialized_scope ? nt.scope : request.visible_environment;
      site.available_locals = native_scope(grammar, genome, verified, choice.ast_begin, nt.category);
      site.replacement_budget = request.budget;
      site.remaining_template_nesting = 256;
      const auto id = result.sites.size();
      result.sites.push_back(std::move(site)); found = groups.emplace(group, id).first;
    }
    auto& site = result.sites[found->second];
    // Forwarded contracts may describe the same physical occurrence twice.
    if (std::any_of(site.occurrences.begin(), site.occurrences.end(), [&](const auto& span) {
      return span.begin == choice.ast_begin && span.end == choice.ast_end;
    })) continue;
    site.occurrences.push_back({choice.ast_begin, choice.ast_end});
    site.occurrence_binder_ids.push_back(choice_environments.at(choice_index));
    site.remaining_template_nesting = std::min(site.remaining_template_nesting, 256 - choice.enclosing_template_depth);
    site.materialized_nodes = choice.ast_end - choice.ast_begin;
    for (auto index = choice.ast_begin; index < choice.ast_end; ++index) {
      site.materialized_depth = std::max(site.materialized_depth, depths[index] - depths[choice.ast_begin] + 1);
      if (witness.nodes[index].template_depth < choice.enclosing_template_depth)
        throw std::logic_error("variation witness has inconsistent physical template depths");
      site.template_nesting = std::max(site.template_nesting,
          witness.nodes[index].template_depth - choice.enclosing_template_depth);
    }
    intersect(site.available_locals, native_scope(grammar, genome, verified, choice.ast_begin, nt.category));
    site.replacement_budget.max_depth = std::min(site.replacement_budget.max_depth,
        request.budget.max_depth - depths.at(choice.ast_begin) + 1);
  }
  for (auto& site : result.sites) {
    // Witness traversal is in prefix order; preserve the parallel binder mapping.
    if (!std::is_sorted(site.occurrences.begin(), site.occurrences.end(), [](const auto& a, const auto& b) {
          return a.begin < b.begin;
        })) throw std::logic_error("variation witness occurrences are not in prefix order");
    std::uint32_t removed = 0, last_end = 0;
    for (const auto& span : site.occurrences) {
      if (span.begin < last_end) throw std::logic_error("logical variation occurrences overlap");
      removed += span.end - span.begin; last_end = span.end;
    }
    site.replacement_budget.max_nodes = static_cast<std::uint32_t>(
        (request.budget.max_nodes - genome.ast.nodes.size() + removed) / site.occurrences.size());
    std::map<std::string, RType> free;
    if (site.category == NodeCategory::Expression) {
      const auto span = site.occurrences.front();
      for (auto index = span.begin; index < span.end; ++index)
        if (genome.ast.nodes[index].kind == NodeKind::VAR)
          free.emplace(genome.ast.names.at(genome.ast.nodes[index].i0), verified.expression_types[index]);
    }
    for (const auto& binding : free) site.free_locals.push_back({binding.first, binding.second});
    for (const auto& binding : site.free_locals)
      if (std::none_of(site.available_locals.begin(), site.available_locals.end(), [&](const auto& available) {
        return binding.name == available.name && binding.type == available.type;
      })) throw std::logic_error("logical variation site has a free local unavailable at an occurrence");
    site.compatibility_key = contract_key(grammar, site);
    if (registry) site.compatibility_id = registry->intern(site.compatibility_key);
  }
  return result;
}

bool compatible_sites(const VariationSite& left, const VariationSite& right) {
  return !left.compatibility_key.empty() && left.compatibility_key == right.compatibility_key;
}

bool donor_fits(const VariationSite& destination, std::uint32_t nodes,
    std::uint32_t depth, std::uint32_t nesting) {
  return nodes > 0 && depth > 0 && nodes <= destination.replacement_budget.max_nodes &&
      depth <= destination.replacement_budget.max_depth && nesting <= destination.remaining_template_nesting;
}

GenerationRequest donor_request(const VariationSite& site) {
  auto budget = site.replacement_budget;
  if (site.category == NodeCategory::Expression) {
    if (budget.max_nodes > 65536 - 4 || budget.max_depth > 256 - 3)
      throw std::invalid_argument("variation donor cannot fit the standalone expression envelope capacity");
    budget.max_nodes += 4; budget.max_depth += 3;
  } else if (site.category != NodeCategory::Program)
    throw std::invalid_argument("variation donor requires an Expression or Program nonterminal");
  return {site.nonterminal, site.type, site.visible_environment, budget};
}

}  // namespace gagp::evo::grammar
