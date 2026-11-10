#pragma once

#include "gagp/evolution/genome.hpp"

namespace gagp::evo::repro {

// Drop unreferenced name/constant table entries. Lvalues are copied; owned
// temporary genomes can transfer their AST tables.
ProgramGenome compact_genome_tables(ProgramGenome genome);

}  // namespace gagp::evo::repro
