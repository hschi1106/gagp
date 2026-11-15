#pragma once

#include "gagp/evolution/genome.hpp"

namespace gagp::evo {

struct ScoredGenome {
  ProgramGenome genome;
  double fitness = 0.0;
  int wrong = -1; // Negative when the task metric has no wrong counts.
};

// Rounds a fitness value to a 40-bit mantissa so that ranking compares the
// same values on every engine (spec/fitness.md).
double canonicalize_fitness_for_ranking(double fitness);

}  // namespace gagp::evo
