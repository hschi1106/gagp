#pragma once

#include "gagp/evolution/grammar/compiled.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/grammar/request.hpp"

namespace gagp::evo::grammar {

// Checks materialized membership independently of seed and provenance. Throws an
// actionable invalid_argument on native, domain, template or budget violations.
void require_membership(const CompiledGrammar& grammar, const ProgramGenome& genome);
void require_membership(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const GenerationRequest& request);

// Deterministic first-matching derivation witness, independent of supplied provenance.
// Validates native structure/types and lowering and preserves logical repeated-hole identities.
// The result is deliberately not seed-replayable.
DerivationMetadata reconstruct_derivation(const CompiledGrammar& grammar, const ProgramGenome& genome);
DerivationMetadata reconstruct_derivation(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const GenerationRequest& request);

// Optional exact native scope annotations for subsequent variation-site analysis.
// Framed annotations describe the private projected AST: node positions and types
// match the contextual AST, while captured external references use synthetic input
// name indexes and require the retained frame/projection for standalone lowering.
DerivationMetadata reconstruct_derivation(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const GenerationRequest& request, VerifiedAst* verified,
    std::vector<std::vector<int>>* choice_lexical_environments = nullptr);

void require_membership_in_frame(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const GenerationRequest& request, const GenerationFrame& frame);
DerivationMetadata reconstruct_derivation_in_frame(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const GenerationRequest& request, const GenerationFrame& frame, VerifiedAst* verified = nullptr,
    std::vector<std::vector<int>>* choice_lexical_environments = nullptr);

}  // namespace gagp::evo::grammar
