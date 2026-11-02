#pragma once
// Research meter of consumed execution fuel per program on the CPU evaluator.
// A caller sets `sink()` on its own thread before eval_fitness_cpu; the
// evaluator then records, per program, the fuel summed over cases and the
// largest single-case fuel. Metering never changes an execution result.
#include <cstdint>
#include <vector>

namespace gagp::detail::fuel_meter {
struct ProgramFuel {
  std::uint64_t sum = 0;
  std::uint32_t max = 0;
};
inline std::vector<ProgramFuel>*& sink() { static thread_local std::vector<ProgramFuel>* value = nullptr; return value; }
// Fuel consumed by the latest top-level execution on this thread.
inline int& last_used() { static thread_local int value = 0; return value; }
}  // namespace gagp::detail::fuel_meter
