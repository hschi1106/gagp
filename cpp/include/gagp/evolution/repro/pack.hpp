#pragma once

#include <vector>

#include "gagp/evolution/genome.hpp"
#include "gagp/evolution/repro/types.hpp"

namespace gagp::evo::repro {

PackedHostData pack_population(const std::vector<ProgramGenome>& population,
                               const PreprocessOutput& prep,
                               const GpuReproConfig& config);

// Lvalues are copied; owned temporary genomes can transfer their AST tables.
ProgramGenome compact_genome_tables(ProgramGenome genome);
std::vector<ProgramGenome> compact_population_tables(const std::vector<ProgramGenome>& population);

}  // namespace gagp::evo::repro
