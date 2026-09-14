#pragma once

#include "gagp/evolution/grammar/variation.hpp"

namespace gagp::evo::grammar::variation_detail {

ProgramGenome certify(ProgramGenome genome, VariationContext& context);
ProgramGenome fallback(const ProgramGenome& certified_parent, VariationContext& context);
ProgramGenome accept(AstProgram candidate, const ProgramGenome& certified_parent,
    VariationContext& context);
AstProgram splice(const AstProgram& base, const VariationSite& destination,
    const AstProgram& donor, VariationSpan payload, const std::vector<int>& donor_binder_ids);

}  // namespace gagp::evo::grammar::variation_detail
