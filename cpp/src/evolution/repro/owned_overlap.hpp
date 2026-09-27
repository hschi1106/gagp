#pragma once

#include <functional>
#include <future>
#include <memory>
#include <vector>

#include "gagp/evolution/repro/gpu.hpp"

namespace gagp::evo::repro {

// Private evolution-loop continuation: the evaluator and preparation worker
// read the same immutable owned population. Prepared state never escapes.
struct OwnedGpuReproOverlap {
  std::shared_ptr<const std::vector<ProgramGenome>> population;
  std::future<std::function<ReproductionResult(const std::vector<double>&)>> completion;
};

OwnedGpuReproOverlap start_owned_gpu_repro_overlap(
    std::vector<ProgramGenome> population, const EvolutionConfig& config,
    std::uint64_t seed, std::shared_ptr<GpuReproRunResources> resources);

}  // namespace gagp::evo::repro
