#pragma once

#include <memory>
#include <functional>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "gagp/core/bytecode.hpp"
#include "gagp/core/errors.hpp"
#include "gagp/core/value.hpp"
#include "gagp/runtime/cpu/fitness_cpu.hpp"

namespace gagp::gpu_detail { class OwnedGpuPrograms; }
namespace gagp::evo::repro { class NativePhasePopulation; }

namespace gagp {

struct FitnessSessionInitTiming {
  double device_select_ms = 0.0;
  double shared_case_pack_ms = 0.0;
  double payload_cache_warm_ms = 0.0;
  double upload_ms = 0.0;
  double total_ms = 0.0;
};

struct FitnessSessionInitResult {
  bool ok = false;
  FitnessSessionInitTiming timing;
  Err err{ErrCode::Value, ""};
};

struct FitnessEvalTiming {
  double pack_ms = 0.0;
  double launch_prep_ms = 0.0;
  double upload_ms = 0.0;
  // CUDA event time when a host overlap callback is supplied; otherwise launch/wait wall time.
  double kernel_ms = 0.0;
  // Separate initial launch/wait wall time and CUDA event time.
  // Initial event time is zero when no host overlap callback is supplied.
  double initial_kernel_wall_ms = 0.0;
  double initial_kernel_event_ms = 0.0;
  double replay_kernel_event_ms = 0.0;
  double copyback_ms = 0.0;
  double teardown_ms = 0.0;
  double total_ms = 0.0;
};

struct FitnessEvalResult {
  std::string execution_profile = "mixed";
  bool ok = false;
  std::vector<double> fitness;
  std::vector<unsigned> wrong; // Present for configured task metrics.
  // Opt-in diagnostics: evaluated cases, errors, timeouts (subset of errors),
  // fallback-token results, and results without a fitness comparison.
  std::vector<std::array<unsigned int, 5>> case_counts;
  // Extra GPU work replacing physical payload failures; logical errors are not replayed.
  std::size_t payload_workspace_budget_bytes = 0; // Configured arena scratch cap.
  std::size_t payload_workspace_retained_bytes = 0; // Session backing, including unused capacity.
  std::size_t payload_workspace_pool_allocations = 0; // Successful backing allocations in this evaluation.
  std::size_t payload_workspace_pool_peak_bytes = 0; // Retained backing plus all current arena scratch.
  // Optional cache diagnostics, including failed attempts/replays: hits, misses, skipped cells.
  std::array<unsigned long long,3> concat_cache_stats{};
  std::size_t payload_replay_programs = 0;
  std::size_t payload_replay_cases = 0;
  std::size_t payload_replay_launches = 0;
  int payload_replay_entry_capacity = 0;
  int payload_replay_value_capacity = 0;
  int initial_payload_entry_capacity = 0; // Zero when the global arena is unused.
  int initial_payload_value_capacity = 0;
  std::size_t initial_payload_physical_blocks = 0;
  bool initial_payload_tiled = false;
  bool arena_cases_sorted = false;
  std::size_t arena_case_order_bytes = 0; // Immutable session input permutation.
  int payload_replay_byte_capacity = 0;
  // Initial physical overflows, before replay replaces the case diagnostics.
  std::vector<unsigned> initial_payload_overflow_cases;
  // Optional row-major [program][ceil(logical_lanes/32)] lane bit masks.
  // Enabled by GAGP_GPU_OVERFLOW_DIAGNOSTICS=1; absent otherwise.
  int initial_payload_overflow_logical_lanes = 0;
  std::vector<unsigned> initial_payload_overflow_lane_masks;
  FitnessEvalTiming timing;
  Err err{ErrCode::Value, ""};
};

class FitnessSessionGpu {
 public:
  FitnessSessionGpu();
  ~FitnessSessionGpu();
  FitnessSessionGpu(const FitnessSessionGpu&) = delete;
  FitnessSessionGpu& operator=(const FitnessSessionGpu&) = delete;
  FitnessSessionGpu(FitnessSessionGpu&&) noexcept;
  FitnessSessionGpu& operator=(FitnessSessionGpu&&) noexcept;

  FitnessSessionInitResult init(const std::vector<CaseBindings>& shared_cases,
                                const std::vector<Value>& shared_answer,
                                int fuel = 10000,
                                int blocksize = 1024,
                                double penalty = 1.0, FitnessPolicy policy = {});
  FitnessEvalResult eval_programs(const std::vector<BytecodeProgram>& programs, bool capture_case_counts = false,
      const std::function<void()>& while_gpu_runs = {}) const;
  bool is_ready() const;

 private:
  friend class evo::repro::NativePhasePopulation;
  FitnessEvalResult eval_owned_programs(const gpu_detail::OwnedGpuPrograms&, bool capture_case_counts) const;
  FitnessEvalResult eval_programs_impl(const std::vector<BytecodeProgram>&, bool,
      const std::function<void()>&, const gpu_detail::OwnedGpuPrograms*) const;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace gagp
