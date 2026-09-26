#include "../region_plan_equal.hpp"
#include "gagp/evolution/grammar/variation.hpp"

#include <stdexcept>
#include <algorithm>
#include <map>
#include <set>
#include <limits>
#include <utility>

#include "variation_internal.hpp"
#include "../subtree_utils.hpp"
#include "gagp/evolution/grammar/values.hpp"
#include "gagp/evolution/node_descriptor.hpp"
#include "gagp/evolution/repro/pack.hpp"
#include "gagp/serialization/region_plan_json.hpp"

namespace gagp::evo::grammar {
namespace {
GenerationRequest checked_entry(const std::shared_ptr<const CompiledGrammar>& grammar) {
  if (!grammar) throw std::invalid_argument("variation context requires a grammar");
  return entry_request(*grammar);
}
}  // namespace

VariationContext::VariationContext(std::shared_ptr<const CompiledGrammar> grammar,
    std::size_t cache_capacity, std::optional<ProjectedBudget> offspring_budget)
    : VariationContext(grammar, checked_entry(grammar), cache_capacity, offspring_budget) {}

VariationContext::VariationContext(std::shared_ptr<const CompiledGrammar> grammar,
    GenerationRequest request, std::size_t cache_capacity, std::optional<ProjectedBudget> offspring_budget)
    : grammar_(std::move(grammar)), request_(std::move(request)), requests_{request_}, cache_(grammar_, cache_capacity, offspring_budget),
      offspring_budget_(offspring_budget), frame_cost_cache_capacity_(cache_capacity) {
  (void)validate_request(*grammar_, request_);
  grammar_->require_executable(request_.nonterminal);
}

VariationContext::VariationContext(std::shared_ptr<const CompiledGrammar> grammar,
    std::vector<GenerationRequest> requests, std::size_t cache_capacity,
    std::optional<ProjectedBudget> offspring_budget)
    : VariationContext(grammar, requests.empty() ? checked_entry(grammar) : requests.front(),
                       cache_capacity, offspring_budget) {
  validate_population_requests(*grammar_, requests);
  requests_ = std::move(requests);
}

std::shared_ptr<const VariationAnalysis> VariationContext::analyze(const ProgramGenome& genome, std::string* runtime_identity) {
  return requests_.size() == 1 ? cache_.analyze(genome, request_, runtime_identity)
                               : cache_.analyze_member(genome, requests_, runtime_identity);
}

std::shared_ptr<const ContextualFrameCostTable> VariationContext::find_frame_costs(
    const std::vector<bool>& local_availability, std::uint32_t effective_depth_limit, GenerationStage stage) {
  const FrameCostCacheKey key{local_availability, effective_depth_limit, stage};
  const auto found = frame_cost_cache_.find(key);
  if (found == frame_cost_cache_.end()) {
    ++frame_cost_cache_counters_.misses;
    return nullptr;
  }
  ++frame_cost_cache_counters_.hits;
  return found->second;
}

std::shared_ptr<const ContextualFrameCostTable> VariationContext::remember_frame_costs(
    const std::vector<bool>& local_availability, std::uint32_t effective_depth_limit,
    std::shared_ptr<const ContextualFrameCostTable> costs, GenerationStage stage) {
  if (!costs) throw std::invalid_argument("variation context cannot cache null frame costs");
  if (frame_cost_cache_capacity_ == 0) return costs;
  const FrameCostCacheKey key{local_availability, effective_depth_limit, stage};
  const auto existing = frame_cost_cache_.find(key);
  if (existing != frame_cost_cache_.end()) return existing->second;
  if (frame_cost_cache_.size() == frame_cost_cache_capacity_) {
    frame_cost_cache_.erase(frame_cost_cache_order_.front());
    frame_cost_cache_order_.pop_front();
    ++frame_cost_cache_counters_.evictions;
  }
  frame_cost_cache_order_.push_back(key);
  frame_cost_cache_.emplace(key, costs);
  return costs;
}

}  // namespace gagp::evo::grammar

namespace gagp::evo::grammar::variation_detail {
namespace {
bool same_materialized_program(const AstProgram& a, const AstProgram& b) {
  if (a.version != b.version || a.nodes.size() != b.nodes.size()) return false;
  if (a.lexical_regions.size() != b.lexical_regions.size() ||
      a.traversal_specs.size() != b.traversal_specs.size() ||
      a.fuel_specs.size() != b.fuel_specs.size() ||
      a.bounded_region_specs.size() != b.bounded_region_specs.size()) return false;
  for (std::size_t i = 0; i < a.lexical_regions.size(); ++i) {
    const auto& left = a.lexical_regions[i];
    const auto& right = b.lexical_regions[i];
    if (left.node_index != right.node_index || left.body_argument != right.body_argument ||
        left.bindings.size() != right.bindings.size()) return false;
    for (std::size_t j = 0; j < left.bindings.size(); ++j)
      if (left.bindings[j].id != right.bindings[j].id ||
          left.bindings[j].type != right.bindings[j].type) return false;
  }
  for (std::size_t i = 0; i < a.traversal_specs.size(); ++i)
    if (a.traversal_specs[i].node_index != b.traversal_specs[i].node_index ||
        a.traversal_specs[i].direction != b.traversal_specs[i].direction) return false;
  for (std::size_t i = 0; i < a.fuel_specs.size(); ++i) {
    const auto& left = a.fuel_specs[i];
    const auto& right = b.fuel_specs[i];
    if (left.node_index != right.node_index || left.charges.size() != right.charges.size()) return false;
    for (std::size_t j = 0; j < left.charges.size(); ++j)
      if (left.charges[j].event != right.charges[j].event ||
          left.charges[j].cost != right.charges[j].cost) return false;
  }
  for (std::size_t i = 0; i < a.bounded_region_specs.size(); ++i) {
    const auto& left = a.bounded_region_specs[i];
    const auto& right = b.bounded_region_specs[i];
    if (left.node_index != right.node_index || left.parameters.size() != right.parameters.size() ||
        left.phases.size() != right.phases.size() ||
        !same_region_plan(left.plan, right.plan)) return false;
    for (std::size_t j = 0; j < left.parameters.size(); ++j) {
      const auto& x = left.parameters[j];
      const auto& y = right.parameters[j];
      if (x.kind != y.kind) return false;
      if (x.kind == RegionCaptureKind::Name) {
        if (a.names.at(x.index) != b.names.at(y.index)) return false;
      } else if (x.index != y.index) return false;
    }
    for (std::size_t j = 0; j < left.phases.size(); ++j) {
      const auto& x = left.phases[j];
      const auto& y = right.phases[j];
      if (x.argument != y.argument || x.bindings.size() != y.bindings.size()) return false;
      for (std::size_t k = 0; k < x.bindings.size(); ++k)
        if (x.bindings[k].binder_id != y.bindings[k].binder_id ||
            x.bindings[k].source.bank != y.bindings[k].source.bank ||
            x.bindings[k].source.slot != y.bindings[k].source.slot) return false;
    }
  }
  const auto same_index = [&](NodeIndexRole role, int left, int right) {
    switch (role) {
      case NodeIndexRole::Unused: return true;
      case NodeIndexRole::Name: return a.names.at(left) == b.names.at(right);
      case NodeIndexRole::Constant:
        return canonical_constant_encoding(a.consts.at(left)) ==
            canonical_constant_encoding(b.consts.at(right));
      case NodeIndexRole::DynamicArity:
      case NodeIndexRole::BinderId:
        return left == right;
    }
    return false;
  };
  for (std::size_t i = 0; i < a.nodes.size(); ++i) {
    const auto& left = a.nodes[i];
    const auto& right = b.nodes[i];
    if (left.kind != right.kind) return false;
    const auto& descriptor = node_descriptor(left.kind);
    if (!same_index(descriptor.i0_role, left.i0, right.i0) ||
        !same_index(descriptor.i1_role, left.i1, right.i1)) return false;
  }
  return true;
}
}  // namespace

ProgramGenome certify(ProgramGenome genome, VariationContext& context) {
  // Validate before compaction so malformed imports cannot reach table remapping.
  auto analysis = context.analyze(genome);
  const auto names = genome.ast.names.size();
  const auto constants = genome.ast.consts.size();
  genome = repro::compact_genome_tables(std::move(genome));
  // Compaction only removes unused entries, preserving the order of survivors.
  // Equal sizes therefore prove every table index and AST annotation unchanged.
  if (genome.ast.names.size() != names || genome.ast.consts.size() != constants)
    analysis = context.analyze(genome);
  // compact_genome_tables already refreshed the native metadata.
  genome.derivation = std::make_shared<const DerivationMetadata>(analysis->witness);
  return genome;
}

ProgramGenome fallback(const ProgramGenome& certified_parent, VariationContext& context) {
  ++context.counters().fallback_children;
  ++context.counters().unchanged_children;
  return certified_parent;
}

ProgramGenome accept(AstProgram candidate, const ProgramGenome& certified_parent,
    VariationContext& context) {
  ProgramGenome child;
  child.ast = std::move(candidate);
  try {
    child = certify(std::move(child), context);
    if (context.requests().size() > 1 && child.derivation->request.nonterminal !=
        context.analyze(certified_parent)->witness.request.nonterminal)
      throw std::invalid_argument("variation changed the parent root contract");
  } catch (const std::invalid_argument&) {
    ++context.counters().acceptance_rejections;
    return fallback(certified_parent, context);
  }
  // certify reconstructed this witness from the candidate AST. Never trust a
  // copied parent's resource index or generation's potentially ambiguous choice.
  if (context.offspring_budget() &&
      !context.offspring_budget()->accepts(child.derivation->resources->subtree())) {
    ++context.counters().budget_rejections;
    return fallback(certified_parent, context);
  }
  // Compare referenced values, not table indices or sharing. Atomic copying can
  // change constant-pool aliasing while leaving every materialized node unchanged.
  if (same_materialized_program(child.ast, certified_parent.ast))
    ++context.counters().unchanged_children;
  else
    ++context.counters().changed_children;
  return child;
}

AstProgram splice(const AstProgram& base, const VariationSite& destination,
    const AstProgram& donor, VariationSpan payload, const std::vector<int>& donor_binder_ids,
    bool closed_crossover) {
  if (closed_crossover && (!destination.crossover_closed || !lexically_closed(donor, payload)))
    throw std::logic_error("closed crossover has an external lexical capture");
  const std::vector<int> empty_ids;
  const auto& source_ids = closed_crossover ? empty_ids : donor_binder_ids;
  AstProgram result = base;
  // Sites come from the certified analysis. Descending physical indices leave
  // earlier original spans stable while all copies receive the identical donor.
  for (std::size_t i = destination.occurrences.size(); i-- > 0;) {
    const auto& span = destination.occurrences[i];
    const auto& target_ids = closed_crossover ? empty_ids : destination.occurrence_binder_ids.at(i);
    if (target_ids.size() != source_ids.size())
      throw std::logic_error("variation donor and destination lexical scopes differ");
    std::map<int, int> remap;
    for (std::size_t j = 0; j < target_ids.size(); ++j)
      remap.emplace(source_ids[j], target_ids[j]);
    AstProgram mapped = donor;
    // A destination capture may use an ID declared inside the donor. Freshen
    // those declarations before mapping captures, so splice alpha-renaming can
    // still distinguish introduced references from captured references.
    std::set<int> used(target_ids.begin(), target_ids.end());
    for (const auto& node : donor.nodes)
      if (node.kind == NodeKind::REGION_VAR) used.insert(node.i0);
    for (const auto& region : donor.lexical_regions)
      for (const auto& binding : region.bindings) used.insert(binding.id);
    for (const auto& region : donor.bounded_region_specs) {
      for (const auto& phase : region.phases)
        for (const auto& binding : phase.bindings) used.insert(binding.binder_id);
      for (const auto& capture : region.parameters)
        if (capture.kind == RegionCaptureKind::Lexical) used.insert(capture.index);
    }
    std::map<int, int> introduced;
    std::set<int> declared;
    int fresh = 0;
    const auto freshen = [&](int& id) {
      declared.insert(id);
      if (std::find(target_ids.begin(), target_ids.end(), id) == target_ids.end()) return;
      while (fresh < std::numeric_limits<int>::max() && used.count(fresh)) ++fresh;
      if (fresh == std::numeric_limits<int>::max())
        throw std::overflow_error("variation exhausted native binder IDs");
      introduced.emplace(id, fresh);
      id = fresh;
      used.insert(fresh);
    };
    for (auto& region : mapped.lexical_regions) {
      if (region.node_index < payload.begin || region.node_index >= payload.end) continue;
      for (auto& binding : region.bindings) freshen(binding.id);
    }
    for (auto& region : mapped.bounded_region_specs) {
      if (region.node_index < payload.begin || region.node_index >= payload.end) continue;
      for (auto& phase : region.phases)
        for (auto& binding : phase.bindings) freshen(binding.binder_id);
    }
    const auto rename_reference = [&](int& id) {
      const auto local = introduced.find(id);
      if (local != introduced.end()) id = local->second;
      else if (!declared.count(id)) {
        const auto found = remap.find(id);
        if (found == remap.end())
          throw std::logic_error("variation donor has an unmapped lexical capture");
        id = found->second;
      }
    };
    for (auto index = payload.begin; index < payload.end; ++index) {
      auto& node = mapped.nodes.at(index);
      if (node.kind == NodeKind::REGION_VAR) rename_reference(node.i0);
    }
    for (auto& region : mapped.bounded_region_specs) {
      if (region.node_index < payload.begin || region.node_index >= payload.end) continue;
      for (auto& capture : region.parameters)
        if (capture.kind == RegionCaptureKind::Lexical) rename_reference(capture.index);
    }
    result = subtree::replace_subtree(result, span.begin, span.end,
        mapped, payload.begin, payload.end);
  }
  return result;
}

}  // namespace gagp::evo::grammar::variation_detail
