#pragma once

#include <vector>

#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/repro/types.hpp"

namespace gagp::evo::repro {

// Private immutable continuation, tied to the exact prepared sources/context.
// Public pack/decode callers have no certificate by default.
struct PreparedParentCertificates {
  std::shared_ptr<const CompiledSpliceSources> sources;
  std::shared_ptr<grammar::VariationContext> context;
  std::vector<GenomeMeta> metadata;
  std::vector<grammar::WarmPopulationMember> analyses;
};

// Accept a completed device operator pass. Metadata reconstruction and cached
// native/grammar verification are host work; all operator choices came from GPU.
std::vector<ProgramGenome> decode_compiled_pass(
    const PackedHostData& packed, const GpuReproChildView& view,
    grammar::VariationContext& context,
    const PreparedParentCertificates* certificates = nullptr);

}  // namespace gagp::evo::repro
