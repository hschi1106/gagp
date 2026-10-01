#include "gagp/evolution/grammar/variation_contract.hpp"
#include "selected_variation.hpp"
#include "gagp/evolution/grammar/resource_projection.hpp"
#include "gagp/evolution/grammar/derivation_resources.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>
#include <string_view>
#include <cstdlib>

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
  field(key, site.crossover_group.empty() ? "exact" : "group");
  if (site.crossover_group.empty()) {
    number(key, site.nonterminal);
    number(key, site.template_id); number(key, site.slot);
  } else {
    field(key, site.crossover_group);
  }
  number(key, static_cast<std::uint32_t>(site.type));
  number(key, static_cast<std::uint32_t>(site.category));
  field(key, site.crossover_closed ? "closed" : "exact-scope");
  if (!site.crossover_closed) {
    number(key, site.context);
    bindings(key, site.visible_environment);
  }
  bindings(key, site.available_locals);
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

bool lexically_closed(const AstProgram& ast, VariationSpan payload) {
  if (payload.begin >= payload.end || payload.end > ast.nodes.size())
    throw std::invalid_argument("invalid lexical closure payload");
  std::set<int> declarations;
  for (const auto& region : ast.lexical_regions)
    if (region.node_index >= payload.begin && region.node_index < payload.end)
      for (const auto& binding : region.bindings) declarations.insert(binding.id);
  for (const auto& region : ast.bounded_region_specs)
    if (region.node_index >= payload.begin && region.node_index < payload.end)
      for (const auto& phase : region.phases)
        for (const auto& binding : phase.bindings) declarations.insert(binding.binder_id);
  for (auto i = payload.begin; i < payload.end; ++i)
    if (ast.nodes[i].kind == NodeKind::REGION_VAR && !declarations.count(ast.nodes[i].i0))
      return false;
  for (const auto& region : ast.bounded_region_specs)
    if (region.node_index >= payload.begin && region.node_index < payload.end)
      for (const auto& capture : region.parameters)
        if (capture.kind == RegionCaptureKind::Lexical && !declarations.count(capture.index))
          return false;
  return true;
}

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

static VariationAnalysis analyze_variation_impl(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const GenerationRequest& initial_request, CompatibilityRegistry* registry,
    const ProjectedBudget* local_projected_budget, const std::vector<GenerationRequest>* requests,
    const variation_detail::SiteSelector* selector = nullptr,
    std::vector<std::size_t>* selected_indices = nullptr, std::size_t* total_sites = nullptr,
    const variation_detail::OwnedScalarPopulation* owned = nullptr, std::size_t owned_index = 0) {
  VariationAnalysis result;
  if (local_projected_budget && !resource_charges_are_local(grammar))
    throw std::invalid_argument("projected candidate allowance requires context-independent resource charges");
  std::vector<std::vector<int>> choice_environments;
  if (!owned) result.witness = requests ?
      reconstruct_population_derivation(grammar, genome, *requests, &result.verified, &choice_environments) :
      reconstruct_derivation(grammar, genome, initial_request, &result.verified, &choice_environments);
  const auto& witness = owned ? owned->witness(owned_index) : result.witness;
  const auto& request = witness.request;
  const auto& verified = owned ? owned->verified(owned_index) : result.verified;
  const auto& environments = owned ? owned->environments(owned_index) : choice_environments;
  std::vector<std::uint32_t> depths(genome.ast.nodes.size());
  std::vector<std::size_t> ends;
  for (std::size_t index = 0; index < depths.size(); ++index) {
    while (!ends.empty() && index >= ends.back()) ends.pop_back();
    depths[index] = static_cast<std::uint32_t>(ends.size() + 1);
    ends.push_back(verified.subtree_end[index]);
  }
  using Group = std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t>;
  std::map<Group, std::size_t> groups;
  std::vector<std::size_t> choice_groups;
  std::vector<bool> selected_groups;
  if (selector) {
    // Enumerate eligibility/group identity only. Expensive per-site scope,
    // resource and compatibility materialization follows selection.
    std::map<Group, std::size_t> logical_groups;
    choice_groups.assign(witness.choices.size(), std::numeric_limits<std::size_t>::max());
    for (std::size_t i = 0; i < witness.choices.size(); ++i) {
      const auto& choice = witness.choices[i];
      const auto& nt = grammar.nonterminals().at(choice.nonterminal);
      const auto& production = grammar.productions().at(choice.production);
      if (!nt.variation_enabled || !production.variation_enabled ||
          (nt.category != NodeCategory::Expression && nt.category != NodeCategory::Program)) continue;
      if (production.unbound_variation) {
        bool bound = false;
        for (auto j = choice.ast_begin; j < choice.ast_end; ++j)
          bound |= genome.ast.nodes[j].kind == NodeKind::REGION_VAR && !witness.nodes[j].fixed;
        if (bound) continue;
      }
      const Group key{choice.nonterminal, witness.nodes.at(choice.ast_begin).logical_instance,
          choice.template_instance, choice.slot};
      const auto inserted = logical_groups.emplace(key, logical_groups.size());
      choice_groups[i] = inserted.first->second;
    }
    *total_sites = logical_groups.size();
    const auto requested = (*selector)(*total_sites);
    selected_groups.assign(*total_sites, false);
    for (auto index : requested) {
      if (index >= *total_sites || selected_groups[index])
        throw std::invalid_argument("selected variation sites must be distinct valid indices");
      selected_groups[index] = true;
    }
    // Build in canonical order, then restore the selector's order below.
    *selected_indices = requested;
  }
  std::vector<std::size_t> built_indices;
  // Within this verified AST/request, scope IDs identify exact locals and
  // nonterminal IDs identify formal environments. The cache never escapes it.
  using ContractShape = std::tuple<std::uint32_t, std::uint32_t, std::uint32_t,
      std::uint32_t, bool, bool, std::string_view>;
  std::vector<ContractShape> shapes;
  std::map<ContractShape, std::string> contract_keys;
  const bool reuse_contracts = std::getenv("GAGP_REUSE_SITE_CONTRACTS") != nullptr;
  for (std::size_t choice_index = 0; choice_index < witness.choices.size(); ++choice_index) {
    if (selector && (choice_groups[choice_index] == std::numeric_limits<std::size_t>::max() ||
                     !selected_groups[choice_groups[choice_index]])) continue;
    const auto& choice = witness.choices[choice_index];
    const auto& nt = grammar.nonterminals().at(choice.nonterminal);
    const auto& production = grammar.productions().at(choice.production);
    if (!nt.variation_enabled || !production.variation_enabled) continue;
    if (production.unbound_variation) {
      bool contains_binding = false;
      for (auto i = choice.ast_begin; i < choice.ast_end; ++i)
        if (genome.ast.nodes[i].kind == NodeKind::REGION_VAR && !witness.nodes[i].fixed) {
          contains_binding = true;
          break;
        }
      if (contains_binding) continue;
    }
    // Complete Program and expression donors share the existing generation request
    // contract. Structural Block/Stmt fragments need their own contextual lowering.
    if (nt.category != NodeCategory::Expression && nt.category != NodeCategory::Program) continue;
    const auto& environment = environments.at(choice_index);
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
      site.crossover_group = grammar.productions().at(choice.production).crossover_group;
      site.crossover_closed = grammar.productions().at(choice.production).closed_crossover;
      site.context = nt.context; site.slot = choice.slot;
      if (choice.template_instance != kNoGrammarId)
        site.template_id = witness.templates.at(choice.template_instance).template_id;
      const auto& ids = environments.at(choice_index);
      const bool materialized_scope = !ids.empty() &&
          std::all_of(ids.begin(), ids.end(), [](int id) { return id >= 0; });
      site.visible_environment = materialized_scope ? nt.scope : request.visible_environment;
      site.available_locals = native_scope(grammar, genome, verified, choice.ast_begin, nt.category);
      site.replacement_budget = request.budget;
      site.remaining_template_nesting = 256;
      const auto id = result.sites.size();
      if (selector) built_indices.push_back(choice_groups[choice_index]);
      if (reuse_contracts)
        shapes.emplace_back(site.nonterminal, site.template_id, site.slot,
            nt.category == NodeCategory::Program ? kNoGrammarId : verified.expression_scope_ids.at(choice.ast_begin),
            materialized_scope, site.crossover_closed, std::string_view(production.crossover_group));
      result.sites.push_back(std::move(site)); found = groups.emplace(group, id).first;
    }
    auto& site = result.sites[found->second];
    // Forwarded contracts may describe the same physical occurrence twice.
    if (std::any_of(site.occurrences.begin(), site.occurrences.end(), [&](const auto& span) {
      return span.begin == choice.ast_begin && span.end == choice.ast_end;
    })) continue;
    site.occurrences.push_back({choice.ast_begin, choice.ast_end});
    site.occurrence_binder_ids.push_back(environments.at(choice_index));
    site.remaining_template_nesting = std::min(site.remaining_template_nesting, 256 - choice.enclosing_template_depth);
    site.materialized_nodes = choice.ast_end - choice.ast_begin;
    site.projected_resources = witness.resources->subtree(choice.ast_begin);
    for (auto index = choice.ast_begin; index < choice.ast_end; ++index) {
      site.materialized_depth = std::max(site.materialized_depth, depths[index] - depths[choice.ast_begin] + 1);
      if (witness.nodes[index].template_depth < choice.enclosing_template_depth)
        throw std::logic_error("variation witness has inconsistent physical template depths");
      site.template_nesting = std::max(site.template_nesting,
          witness.nodes[index].template_depth - choice.enclosing_template_depth);
    }
    // The first occurrence initialized available_locals when the site was created.
    // Only linked later occurrences need another scope lookup and intersection.
    if (site.occurrences.size() > 1)
      intersect(site.available_locals, native_scope(grammar, genome, verified, choice.ast_begin, nt.category));
    site.replacement_budget.max_depth = std::min(site.replacement_budget.max_depth,
        request.budget.max_depth - depths.at(choice.ast_begin) + 1);
  }
  std::size_t site_index = 0;
  for (auto& site : result.sites) {
    if (local_projected_budget) {
      std::vector<std::size_t> roots;
      for (const auto& span : site.occurrences) roots.push_back(span.begin);
      site.has_projected_allowance = true;
      site.projected_allowance = witness.resources->replacement(roots,
          local_projected_budget->max_nodes, local_projected_budget->max_depth);
    }
    // Witness traversal is in prefix order; preserve the parallel binder mapping.
    if (!std::is_sorted(site.occurrences.begin(), site.occurrences.end(), [](const auto& a, const auto& b) {
          return a.begin < b.begin;
        })) throw std::logic_error("variation witness occurrences are not in prefix order");
    std::uint32_t removed = 0, last_end = 0;
    for (const auto& span : site.occurrences) {
      if (span.begin < last_end) throw std::logic_error("logical variation occurrences overlap");
      removed += span.end - span.begin; last_end = span.end;
    }
    // The verified parent fits physical limits, so the depth limit is a proved
    // upper bound for untouched nodes. Unit charges need no weighted index or
    // extra per-site allocation; use the same allowance formula as projections.
    const auto allowance = resource_detail::allowance_after_validation(genome.ast.nodes.size(), removed,
        site.occurrences.size(), request.budget.max_depth - site.replacement_budget.max_depth,
        request.budget.max_nodes, request.budget.max_depth);
    site.replacement_budget.max_nodes = static_cast<std::uint32_t>(allowance.max_nodes);
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
    if (site.crossover_closed)
      for (const auto& span : site.occurrences)
        site.crossover_closed = site.crossover_closed && lexically_closed(genome.ast, span);
    if (reuse_contracts && site.occurrences.size() == 1) {
      auto shape = shapes.at(site_index);
      std::get<5>(shape) = site.crossover_closed;
      const auto cached = contract_keys.find(shape);
      if (cached != contract_keys.end()) site.compatibility_key = cached->second;
      else {
        site.compatibility_key = contract_key(grammar, site);
        contract_keys.emplace(shape, site.compatibility_key);
      }
    } else site.compatibility_key = contract_key(grammar, site);
    ++site_index;
    if (registry) site.compatibility_id = registry->intern(site.compatibility_key);
  }
  if (selector) {
    std::vector<VariationSite> ordered;
    ordered.reserve(selected_indices->size());
    for (auto index : *selected_indices) {
      const auto found = std::find(built_indices.begin(), built_indices.end(), index);
      if (found == built_indices.end()) throw std::logic_error("selected logical site was not built");
      ordered.push_back(std::move(result.sites[static_cast<std::size_t>(found - built_indices.begin())]));
    }
    result.sites = std::move(ordered);
  }
  return result;
}

VariationAnalysis analyze_variation(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const GenerationRequest& request, CompatibilityRegistry* registry,
    const ProjectedBudget* local_projected_budget) {
  return analyze_variation_impl(grammar, genome, request, registry, local_projected_budget, nullptr);
}

VariationAnalysis analyze_population_variation(const CompiledGrammar& grammar,
    const ProgramGenome& genome, const std::vector<GenerationRequest>& requests,
    CompatibilityRegistry* registry, const ProjectedBudget* local_projected_budget) {
  if (requests.empty()) throw std::invalid_argument("population requires a root request");
  return analyze_variation_impl(grammar, genome, requests.front(), registry, local_projected_budget, &requests);
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
  return {site.nonterminal, site.type, site.visible_environment, budget, GenerationStage::Mutation};
}

bool donor_fits(const VariationSite& destination, const VariationSite& donor) {
  return donor_fits(destination, donor.materialized_nodes, donor.materialized_depth, donor.template_nesting) &&
      (!destination.has_projected_allowance || destination.projected_allowance.accepts(donor.projected_resources));
}

namespace variation_detail {
SelectedOwnedSites analyze_selected_owned(const OwnedScalarPopulation& owner,
    std::size_t index, const SiteSelector& select, CompatibilityRegistry* registry) {
  SelectedOwnedSites selected;
  auto result = analyze_variation_impl(*owner.grammar_owner(), owner.genomes().at(index),
      owner.witness(index).request, registry, nullptr, nullptr, &select,
      &selected.original_indices, &selected.total_sites, &owner, index);
  selected.sites = std::move(result.sites);
  return selected;
}

SelectedVariationAnalysis analyze_selected_variation(const CompiledGrammar& grammar,
    const ProgramGenome& genome, const std::vector<GenerationRequest>& requests,
    const SiteSelector& select, CompatibilityRegistry* registry,
    const ProjectedBudget* local_projected_budget) {
  if (requests.empty()) throw std::invalid_argument("population requires a root request");
  SelectedVariationAnalysis selected;
  auto result = analyze_variation_impl(grammar, genome, requests.front(), registry,
      local_projected_budget, &requests, &select, &selected.original_indices, &selected.total_sites);
  selected.witness = std::move(result.witness);
  selected.verified = std::move(result.verified);
  selected.sites = std::move(result.sites);
  return selected;
}

VariationAnalysis remap_compacted_analysis(const CompiledGrammar& grammar,
    const VariationAnalysis& source, const AstProgram& before, const AstProgram& after) {
  VariationAnalysis result = source;
  std::vector<int> remap(before.names.size(), -1);
  std::size_t next = 0;
  for (std::size_t i = 0; i < before.names.size(); ++i)
    if (next < after.names.size() && before.names[i] == after.names[next])
      remap[i] = static_cast<int>(next++);
  if (next != after.names.size()) throw std::logic_error("analysis compaction is not stable");
  using ScopeKey = std::pair<std::vector<std::pair<int, RType>>, std::vector<std::pair<int, RType>>>;
  std::map<ScopeKey, std::uint32_t> scope_ids;
  std::vector<std::uint32_t> remapped_scopes;
  std::vector<VerifiedScope> scopes;
  std::vector<std::uint64_t> signatures;
  for (const auto& original : source.verified.scopes) {
    VerifiedScope scope;
    scope.binders = original.binders;
    for (const auto& entry : original.locals)
      if (remap.at(entry.first) >= 0) scope.locals.emplace_back(remap[entry.first], entry.second);
    ScopeKey key{scope.locals, scope.binders};
    auto found = scope_ids.find(key);
    if (found == scope_ids.end()) {
      const auto id = static_cast<std::uint32_t>(scopes.size());
      found = scope_ids.emplace(std::move(key), id).first;
      std::uint64_t signature = 1469598103934665603ULL;
      for (const auto& entry : scope.locals) {
        signature ^= static_cast<std::uint64_t>(entry.first + 1);
        signature *= 1099511628211ULL;
        signature ^= static_cast<std::uint64_t>(entry.second) + 1ULL;
        signature *= 1099511628211ULL;
      }
      signatures.push_back(signature);
      scopes.push_back(std::move(scope));
    }
    remapped_scopes.push_back(found->second);
  }
  for (std::size_t i = 0; i < result.verified.expression_scope_ids.size(); ++i) {
    auto& id = result.verified.expression_scope_ids[i];
    if (id == kNoGrammarId) continue;
    id = remapped_scopes.at(id);
    result.verified.expression_scope_signatures[i] = signatures.at(id);
  }
  result.verified.scopes = std::move(scopes);
  std::set<std::string> names(after.names.begin(), after.names.end());
  for (auto& site : result.sites) {
    const auto prior_count = site.available_locals.size();
    if (site.category != NodeCategory::Program)
      site.available_locals.erase(std::remove_if(site.available_locals.begin(), site.available_locals.end(),
          [&](const auto& binding) { return !names.count(binding.name); }), site.available_locals.end());
    // The original IDs belong to the same live context registry. Stable table
    // renumbering changes compatibility only when an unused input disappears.
    if (site.available_locals.size() != prior_count) {
      site.compatibility_key = contract_key(grammar, site);
      site.compatibility_id = kNoGrammarId;
    }
  }
  return result;
}
}  // namespace variation_detail

}  // namespace gagp::evo::grammar
