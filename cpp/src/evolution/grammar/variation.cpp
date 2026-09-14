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

namespace gagp::evo::grammar {
namespace {
GenerationRequest checked_entry(const std::shared_ptr<const CompiledGrammar>& grammar) {
  if (!grammar) throw std::invalid_argument("variation context requires a grammar");
  return entry_request(*grammar);
}
}  // namespace

VariationContext::VariationContext(std::shared_ptr<const CompiledGrammar> grammar,
    std::size_t cache_capacity)
    : VariationContext(grammar, checked_entry(grammar), cache_capacity) {}

VariationContext::VariationContext(std::shared_ptr<const CompiledGrammar> grammar,
    GenerationRequest request, std::size_t cache_capacity)
    : grammar_(std::move(grammar)), request_(std::move(request)), cache_(grammar_, cache_capacity) {
  (void)validate_request(*grammar_, request_);
  grammar_->require_executable(request_.nonterminal);
}

}  // namespace gagp::evo::grammar

namespace gagp::evo::grammar::variation_detail {
namespace {
bool same_materialized_program(const AstProgram& a, const AstProgram& b) {
  if (a.version != b.version || a.nodes.size() != b.nodes.size()) return false;
  if (a.lexical_regions.size() != b.lexical_regions.size() ||
      a.traversal_specs.size() != b.traversal_specs.size() ||
      a.fuel_specs.size() != b.fuel_specs.size()) return false;
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
  const auto same_index = [&](NodeIndexRole role, int left, int right) {
    switch (role) {
      case NodeIndexRole::Unused: return true;
      case NodeIndexRole::Name: return a.names.at(left) == b.names.at(right);
      case NodeIndexRole::Constant:
        return canonical_json(encode_constant(a.consts.at(left))) ==
            canonical_json(encode_constant(b.consts.at(right)));
      case NodeIndexRole::BinderId:
      case NodeIndexRole::ListTypeTag: return left == right;
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
  (void)context.cache().analyze(genome, context.request());
  genome = repro::compact_genome_tables(genome);
  const auto analysis = context.cache().analyze(genome, context.request());
  genome.meta = build_genome_meta(genome.ast);
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
  } catch (const std::invalid_argument&) {
    ++context.counters().acceptance_rejections;
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
    const AstProgram& donor, VariationSpan payload, const std::vector<int>& donor_binder_ids) {
  AstProgram result = base;
  // Sites come from the certified analysis. Descending physical indices leave
  // earlier original spans stable while all copies receive the identical donor.
  for (std::size_t i = destination.occurrences.size(); i-- > 0;) {
    const auto& span = destination.occurrences[i];
    const auto& target_ids = destination.occurrence_binder_ids.at(i);
    if (target_ids.size() != donor_binder_ids.size())
      throw std::logic_error("variation donor and destination lexical scopes differ");
    std::map<int, int> remap;
    for (std::size_t j = 0; j < target_ids.size(); ++j)
      remap.emplace(donor_binder_ids[j], target_ids[j]);
    AstProgram mapped = donor;
    // A destination capture may use an ID declared inside the donor. Freshen
    // those declarations before mapping captures, so splice alpha-renaming can
    // still distinguish introduced references from captured references.
    std::set<int> used(target_ids.begin(), target_ids.end());
    for (const auto& node : donor.nodes)
      if (node.kind == NodeKind::REGION_VAR) used.insert(node.i0);
    for (const auto& region : donor.lexical_regions)
      for (const auto& binding : region.bindings) used.insert(binding.id);
    std::map<int, int> introduced;
    std::set<int> declared;
    int fresh = 0;
    for (auto& region : mapped.lexical_regions) {
      if (region.node_index < payload.begin || region.node_index >= payload.end) continue;
      for (auto& binding : region.bindings) {
        declared.insert(binding.id);
        if (std::find(target_ids.begin(), target_ids.end(), binding.id) == target_ids.end()) continue;
        while (used.count(fresh)) ++fresh;
        if (fresh == std::numeric_limits<int>::max())
          throw std::overflow_error("variation exhausted native binder IDs");
        introduced.emplace(binding.id, fresh);
        binding.id = fresh; used.insert(fresh);
      }
    }
    for (auto index = payload.begin; index < payload.end; ++index) {
      auto& node = mapped.nodes.at(index);
      if (node.kind != NodeKind::REGION_VAR) continue;
      const auto local = introduced.find(node.i0);
      if (local != introduced.end()) node.i0 = local->second;
      else if (!declared.count(node.i0)) {
        const auto found = remap.find(node.i0);
        if (found == remap.end())
          throw std::logic_error("variation donor has an unmapped lexical capture");
        node.i0 = found->second;
      }
    }
    result = subtree::replace_subtree(result, span.begin, span.end,
        mapped, payload.begin, payload.end);
  }
  return result;
}

}  // namespace gagp::evo::grammar::variation_detail
