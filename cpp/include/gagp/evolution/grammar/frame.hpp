#pragma once

#include <vector>

#include "gagp/evolution/grammar/request.hpp"
#include "gagp/evolution/input_spec.hpp"

namespace gagp::evo::grammar {

// Runtime locals available to an isolated generated expression. These are
// verifier inputs for the donor only; they do not alter the compiled grammar.
struct GenerationFrame {
  std::vector<RegionBinding> locals;
  // Native IDs aligned with request.visible_environment; empty means no external
  // lexical values are supplied. These IDs are never ordinary local names.
  std::vector<int> binder_ids;
};

std::vector<InputSpec> frame_inputs(const CompiledGrammar& grammar,
    const GenerationRequest& request, const GenerationFrame& frame);

// Ordered native IDs for the requested nonterminal's formal scope.
std::vector<int> frame_environment(const CompiledGrammar& grammar,
    const GenerationRequest& request, const GenerationFrame& frame);

struct FramedProgram {
  AstProgram ast;
  std::vector<InputSpec> inputs;
};

// Private donor validation/lowering view. Only captured references become fresh
// synthetic inputs; introduced region bindings retain their native identity.
// The original AST remains contextual and cannot be executed as a closed artifact.
FramedProgram project_frame(const CompiledGrammar& grammar,
    const GenerationRequest& request, const GenerationFrame& frame, const AstProgram& ast);

}  // namespace gagp::evo::grammar
