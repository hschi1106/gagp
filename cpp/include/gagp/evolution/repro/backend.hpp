#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/repro/stats.hpp"
#include "gagp/evolution/selection.hpp"

namespace gagp::evo {
struct EvolutionConfig;
}

namespace gagp::evo::repro {

enum class ReproductionBackend {
  Cpu,
  Gpu,
};

struct ReproductionResult {
  std::vector<ProgramGenome> next_population;
  ReproductionStats stats;
};

void require_reproduction_mode_supported(const EvolutionConfig& cfg);

// Native AST reproduction; it always runs on the host.
ReproductionResult run_reproduction_backend(const std::vector<ScoredGenome>& scored,
                                            const EvolutionConfig& cfg,
                                            std::mt19937_64& rng);
ReproductionResult run_reproduction_backend(const std::vector<ScoredGenomeRef>& scored,
                                            const EvolutionConfig& cfg,
                                            std::mt19937_64& rng);

}  // namespace gagp::evo::repro
