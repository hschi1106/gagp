#pragma once

#include <vector>

#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/input_spec.hpp"
#include "gagp/migration/legacy_ast_v1.hpp"

namespace gagp::evo::transition {

// Migration-oracle adapter. LinearRec is lowered first through its existing
// adapter, then legacy DC/DP nodes are replaced with general bounded regions.
ProgramGenome lower_bounded_regions(
    const migration::legacy_v1::AstProgram& source,
    const std::vector<InputSpec>& inputs = {});

}  // namespace gagp::evo::transition
