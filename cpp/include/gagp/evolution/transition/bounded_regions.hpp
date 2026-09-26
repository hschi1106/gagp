#pragma once

#include <vector>

#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/input_spec.hpp"
#include "gagp/migration/legacy_ast_v1.hpp"

namespace gagp::evo::transition {

// Migration-oracle adapter. LinearRec is lowered first through its existing
// adapter, then legacy DC/DP nodes are replaced with general bounded regions.
// A positive minimum_dc_frames reserves additional DC stack capacity for an
// evolving mapped population. It never reduces the source-derived bound and
// does not change fuel or any DP region. The caller still checks GPU capacity.
ProgramGenome lower_bounded_regions(
    const migration::legacy_v1::AstProgram& source,
    const std::vector<InputSpec>& inputs = {},
    std::uint32_t minimum_dc_frames = 0);

}  // namespace gagp::evo::transition
