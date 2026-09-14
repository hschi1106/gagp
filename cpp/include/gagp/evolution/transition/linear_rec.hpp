#pragma once

#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/input_spec.hpp"

namespace gagp::evo::transition {

// Migration oracle adapter, separate from production generation/runtime paths.
// Replaces LinearRec with general primitives and explicit semantic fuel events.
ProgramGenome lower_linear_rec(const ProgramGenome& source,
    const std::vector<InputSpec>& inputs = {});

}  // namespace gagp::evo::transition
