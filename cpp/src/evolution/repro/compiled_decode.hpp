#pragma once

#include <vector>

#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/repro/types.hpp"

namespace gagp::evo::repro {

// Accept a completed device operator pass. Metadata reconstruction and cached
// native/grammar verification are host work; all operator choices came from GPU.
std::vector<ProgramGenome> decode_compiled_pass(
    const PackedHostData& packed, const GpuReproChildView& view,
    grammar::VariationContext& context);

}  // namespace gagp::evo::repro
