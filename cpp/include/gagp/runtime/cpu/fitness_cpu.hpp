#pragma once

#include <vector>

#include "gagp/core/bytecode.hpp"
#include "gagp/core/value.hpp"
#include "gagp/core/fitness_policy.hpp"

namespace gagp {

struct InputBinding {
  int idx = 0;
  Value value = Value::invalid();
};

using CaseBindings = std::vector<InputBinding>;

// Optional per-program execution counts. Errors include timeouts; scoring
// mismatches and fallback values are not runtime errors.
struct CpuFitnessCaseCounts {
  unsigned cases = 0;
  unsigned errors = 0;
  unsigned timeouts = 0;
};

// Returns one fitness score per program.
// Per-case scoring:
//   if expected is numeric:
//     actual numeric => -abs(actual - expected)
//     actual non-numeric => -penalty
//   Bool/Char/String/IntList/FloatList/StringList => exact match ? 1 : 0
//     type mismatch => -penalty
//   runtime error => -penalty
// All programs share one case set.
std::vector<double> eval_fitness_cpu(
    const std::vector<BytecodeProgram>& programs,
    const std::vector<CaseBindings>& shared_cases,
    const std::vector<Value>& shared_answer,
    int fuel = 10000,
    double penalty = 1.0,
    int reduction_lanes = 1,
    FitnessPolicy policy = {}, std::vector<unsigned>* wrong = nullptr,
    std::vector<CpuFitnessCaseCounts>* case_counts = nullptr);

}  // namespace gagp
