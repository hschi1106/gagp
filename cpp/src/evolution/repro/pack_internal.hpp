#pragma once
#include "gagp/evolution/repro/pack.hpp"
#include "gagp/evolution/grammar/variation_cache.hpp"

namespace gagp::evo::repro {
// Private continuation for the unchanged population/context that produced these
// warm rows. Every reused identity still requires current payload reads.
PackedHostData pack_warmed_population(const std::vector<ProgramGenome>& population,
    const PreprocessOutput& prep, const GpuReproConfig& config,
    const std::vector<grammar::WarmPopulationMember>& warmed);
}
