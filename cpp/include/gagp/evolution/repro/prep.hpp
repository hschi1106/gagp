#pragma once

#include <memory>
#include <vector>

#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/repro/types.hpp"

namespace gagp::evo {
namespace grammar { class VariationContext; }
struct EvolutionConfig;
}

namespace gagp::evo::repro {

struct ConstantMutationDomains;

// Compiled preparation shared by CPU and GPU reproduction dispatch.
PreprocessOutput preprocess_population(const std::vector<ProgramGenome>& population,
    const GpuReproConfig& config, grammar::VariationContext& context,
    std::shared_ptr<const ConstantMutationDomains> domains = nullptr);

GpuReproConfig make_gpu_repro_config(const std::vector<ProgramGenome>& population,
                                     const EvolutionConfig& cfg);

}  // namespace gagp::evo::repro
