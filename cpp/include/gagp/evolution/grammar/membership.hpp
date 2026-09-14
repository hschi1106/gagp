#pragma once

#include "gagp/evolution/grammar/compiled.hpp"
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/grammar/request.hpp"

namespace gagp::evo::grammar {

// Checks materialized membership independently of seed and provenance. Throws an
// actionable invalid_argument on native, domain, template or budget violations.
void require_membership(const CompiledGrammar& grammar, const ProgramGenome& genome);
void require_membership(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const GenerationRequest& request);

}  // namespace gagp::evo::grammar
