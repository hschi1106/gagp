#pragma once
#include "../grammar/owned_population.hpp"

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
  // Private scalar-only owned admission: tied to sources/context above, with no
  // mutable registry constants and no incomplete public variation analysis.
  std::vector<ProgramGenome> admitted_parents;
  std::shared_ptr<const grammar::variation_detail::OwnedScalarPopulation> owned_parents;
};

// Accept a completed device operator pass. Metadata reconstruction and cached
// native/grammar verification are host work; all operator choices came from GPU.
std::vector<ProgramGenome> decode_compiled_pass(
    const PackedHostData& packed, const GpuReproChildView& view,
    grammar::VariationContext& context,
    const PreparedParentCertificates* certificates = nullptr);

}  // namespace gagp::evo::repro
