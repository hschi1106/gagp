#pragma once

#include <random>
#include <vector>

#include "gagp/evolution/repro/backend.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/repro/types.hpp"

namespace gagp::evo {
struct EvolutionConfig;
}

namespace gagp::evo::repro {

// Immutable grammar resources shared across generations; mutable analysis stays
// in each preparation worker. Payload roots must participate in registry sweeps.
struct GpuReproRunResources;
std::shared_ptr<GpuReproRunResources> make_gpu_repro_run_resources(const EvolutionConfig& cfg);
void append_gpu_repro_run_payload_roots(
    const std::shared_ptr<GpuReproRunResources>& resources, std::vector<Value>* roots);

struct PreparedParentCertificates;

struct GpuReproPreparedData {
  GpuReproConfig config;
  PackedHostData packed;
  // A completed preparation owns its worker context through both device passes.
  // Prepared runs are sequential, like the shared GPU arena they use.
  std::shared_ptr<grammar::VariationContext> compiled_context;
  grammar::VariationCounters preparation_counters;
  std::shared_ptr<const PreparedParentCertificates> parent_certificates;
  std::shared_ptr<GpuReproRunResources> run_resources;

};

GpuReproPreparedData prepare_gpu_repro_backend_inputs(const std::vector<ProgramGenome>& population,
                                                      const EvolutionConfig& cfg,
                                                      std::uint64_t seed,
                                                      ReproductionStats* stats = nullptr,
                                                      std::shared_ptr<GpuReproRunResources> resources = nullptr);

ReproductionResult run_gpu_repro_backend_prepared(const std::vector<ScoredGenome>& scored,
                                                  const EvolutionConfig& cfg,
                                                  const GpuReproPreparedData& prepared,
                                                  ReproductionStats* stats = nullptr);
ReproductionResult run_gpu_repro_backend_prepared(const std::vector<ScoredGenomeRef>& scored,
                                                  const EvolutionConfig& cfg,
                                                  const GpuReproPreparedData& prepared,
                                                  ReproductionStats* stats = nullptr);

ReproductionResult run_gpu_repro_backend(const std::vector<ScoredGenome>& scored,
                                         const EvolutionConfig& cfg,
                                         std::mt19937_64& rng,
                                         std::shared_ptr<GpuReproRunResources> resources = nullptr);
ReproductionResult run_gpu_repro_backend(const std::vector<ScoredGenomeRef>& scored,
                                         const EvolutionConfig& cfg,
                                         std::mt19937_64& rng,
                                         std::shared_ptr<GpuReproRunResources> resources = nullptr);

}  // namespace gagp::evo::repro
