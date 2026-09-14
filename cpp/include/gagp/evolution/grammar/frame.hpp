#pragma once

#include <vector>

#include "gagp/evolution/grammar/request.hpp"
#include "gagp/evolution/input_spec.hpp"

namespace gagp::evo::grammar {

// Runtime locals available to an isolated generated expression. These are
// verifier inputs for the donor only; they do not alter the compiled grammar.
struct GenerationFrame {
  std::vector<RegionBinding> locals;
};

std::vector<InputSpec> frame_inputs(const CompiledGrammar& grammar,
    const GenerationRequest& request, const GenerationFrame& frame);

}  // namespace gagp::evo::grammar
