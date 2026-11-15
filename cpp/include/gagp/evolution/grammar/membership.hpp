#pragma once

#include "gagp/evolution/grammar/compiled.hpp"
#include "gagp/core/bytecode.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/evolution/grammar/request.hpp"

namespace gagp::evo::grammar {


// Returns only an immutable executable sealed by canonical admission and matching
// current AST/scalar constants/input order/fuel. Not a grammar-admission API.
// Registry-dependent constants and stale/unsealed proofs return nullptr.
std::shared_ptr<const BytecodeProgram> admitted_bytecode_for_eval(
    const ProgramGenome& genome, const std::vector<std::string>& input_names,
    std::uint32_t fuel);

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

// Optional exact native scope annotations: `verified` receives the verified
// AST and `choice_lexical_environments` the native binder IDs of every
// choice's lexical scope, aligned with the witness choices.
DerivationMetadata reconstruct_derivation(const CompiledGrammar& grammar, const ProgramGenome& genome,
    const GenerationRequest& request, VerifiedAst* verified,
    std::vector<std::vector<int>>* choice_lexical_environments = nullptr);

}  // namespace gagp::evo::grammar
