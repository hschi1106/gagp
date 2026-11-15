#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "gagp/runtime/cpu/fitness_cpu.hpp"

namespace gagp {

// eval_fitness_cpu with an explicit physical overlay per case, so tests can
// reach exhaustion without allocating the production overlay.
std::vector<double> eval_fitness_cpu_with_overlay(
    const std::vector<BytecodeProgram>& programs,
    const std::vector<CaseBindings>& shared_cases,
    const std::vector<Value>& shared_answer,
    int fuel, double penalty, int reduction_lanes, FitnessPolicy policy,
    std::vector<unsigned>* wrong, std::vector<CpuFitnessCaseCounts>* case_counts,
    std::size_t case_payload_bytes);

// The error that stops an evaluation whose case exhausted the overlay.
std::string physical_exhaustion_message(std::size_t case_payload_bytes);

}  // namespace gagp
