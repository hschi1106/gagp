#pragma once

#include <optional>

#include "gagp/evolution/grammar/variation.hpp"

namespace gagp::evo::grammar::variation_detail {

// Optional owned copies preserve the original-parent coordinates for coupled
// mutation after crossover. Normal crossover does not copy this metadata.
using SelectedCrossoverSites = std::optional<std::pair<VariationSite, VariationSite>>;
std::pair<ProgramGenome, ProgramGenome> crossover_with_sites(
    const ProgramGenome& parent_a, const ProgramGenome& parent_b,
    std::uint64_t seed, VariationContext& context, SelectedCrossoverSites* selected);

ProgramGenome certify(ProgramGenome genome, VariationContext& context);
ProgramGenome fallback(const ProgramGenome& certified_parent, VariationContext& context);
ProgramGenome accept(AstProgram candidate, const ProgramGenome& certified_parent,
    VariationContext& context);
AstProgram splice(const AstProgram& base, const VariationSite& destination,
    const AstProgram& donor, VariationSpan payload, const std::vector<int>& donor_binder_ids,
    bool closed_crossover = false);

}  // namespace gagp::evo::grammar::variation_detail
