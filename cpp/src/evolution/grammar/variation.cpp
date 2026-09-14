#include "gagp/evolution/grammar/variation.hpp"

#include <stdexcept>
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
  const auto same_index = [&](NodeIndexRole role, int left, int right) {
    switch (role) {
      case NodeIndexRole::Unused: return true;
      case NodeIndexRole::Name: return a.names.at(left) == b.names.at(right);
      case NodeIndexRole::Constant:
        return canonical_json(encode_constant(a.consts.at(left))) ==
            canonical_json(encode_constant(b.consts.at(right)));
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
    const AstProgram& donor, VariationSpan payload) {
  AstProgram result = base;
  // Sites come from the certified analysis. Descending physical indices leave
  // earlier original spans stable while all copies receive the identical donor.
  for (auto span = destination.occurrences.rbegin(); span != destination.occurrences.rend(); ++span)
    result = subtree::replace_subtree(result, span->begin, span->end,
        donor, payload.begin, payload.end);
  return result;
}

}  // namespace gagp::evo::grammar::variation_detail
