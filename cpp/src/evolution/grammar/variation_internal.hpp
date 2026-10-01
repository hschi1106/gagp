#pragma once

#include <optional>

#include "gagp/evolution/grammar/variation.hpp"

namespace gagp::evo::grammar {
VariationAnalysis analyze_closed_phase(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const GenerationRequest& request, const GenerationFrame& frame);
}

namespace gagp::evo::grammar::variation_detail {

// Optional owned copies preserve the original-parent coordinates for coupled
// mutation after crossover. Normal crossover does not copy this metadata.
using SelectedCrossoverSites = std::optional<std::pair<VariationSite, VariationSite>>;
std::pair<ProgramGenome, ProgramGenome> crossover_with_sites(
    const ProgramGenome& parent_a, const ProgramGenome& parent_b,
    std::uint64_t seed, VariationContext& context, SelectedCrossoverSites* selected);

// Internal stable-table compaction continuation; the caller owns the exact
// validated source and compacted AST and validates their payload snapshots.
VariationAnalysis remap_compacted_analysis(const CompiledGrammar& grammar,
    const VariationAnalysis& source, const AstProgram& before, const AstProgram& after);

ProgramGenome certify(ProgramGenome genome, VariationContext& context);
ProgramGenome fallback(const ProgramGenome& certified_parent, VariationContext& context);
// The optional root is private owned-source evidence. Its caller must validate
// the parent certificate's payload snapshot before work and before publication.
// Candidate membership, lowering, budget and root-contract checks still run.
ProgramGenome accept(AstProgram candidate, const ProgramGenome& certified_parent,
    VariationContext& context, std::optional<std::uint32_t> owned_parent_root = std::nullopt);
AstProgram splice(const AstProgram& base, const VariationSite& destination,
    const AstProgram& donor, VariationSpan payload, const std::vector<int>& donor_binder_ids,
    bool closed_crossover = false);

}  // namespace gagp::evo::grammar::variation_detail
