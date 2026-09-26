#pragma once

#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/grammar/request.hpp"
#include "gagp/evolution/grammar/resource_projection.hpp"

namespace gagp::evo::grammar {

// Sufficient certificate for transplanting resource costs: every materialized
// native kind/fuel-profile pair has one authored charge throughout the grammar.
// False is conservative; it does not imply that the grammar is ambiguous.
bool resource_charges_are_local(const CompiledGrammar& grammar);

// Sufficient per-root proof that all derivations of one materialized tree
// assign the same charge to every node. Constraints are relaxed conservatively.
// Incomplete/capacity-limited analysis certifies no roots. This does not prove
// that a mutation-entry donor belongs to a destination nonterminal.
struct ResourceInvarianceCertificate {
  bool complete = false;
  std::vector<bool> roots;
  std::size_t product_states = 0;
};
ResourceInvarianceCertificate certify_resource_invariance(const CompiledGrammar& grammar,
    const std::vector<std::uint32_t>& roots, std::size_t work_limit = 100000000);

// Reconstructs canonical membership rather than trusting attached provenance.
// Unauthored nodes (including implicit expression envelopes) have unit charges.
// Checks native validity and physical limits, but does not compile bytecode.
// Full derivation reconstruction and lowering checks still govern admission.
ResourceProjection project_derivation_resources(const CompiledGrammar& grammar,
    const ProgramGenome& genome, const GenerationRequest& request);
ResourceProjection project_derivation_resources(const CompiledGrammar& grammar,
    const ProgramGenome& genome);

}  // namespace gagp::evo::grammar
