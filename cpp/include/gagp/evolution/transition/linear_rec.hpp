#pragma once

#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/input_spec.hpp"
#include "gagp/migration/legacy_ast_v1.hpp"

namespace gagp::evo::transition {

// Migration oracle adapter, separate from production generation/runtime paths.
// Replaces LinearRec with general primitives and explicit semantic fuel events.
ProgramGenome lower_linear_rec(const migration::legacy_v1::AstProgram& source,
    const std::vector<InputSpec>& inputs = {});

}  // namespace gagp::evo::transition
