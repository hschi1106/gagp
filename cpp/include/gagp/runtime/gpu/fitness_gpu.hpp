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
  double copyback_ms = 0.0;
  double teardown_ms = 0.0;
  double total_ms = 0.0;
};

struct FitnessEvalResult {
  std::string execution_profile = "mixed";
  bool ok = false;
  std::vector<double> fitness;
  // Opt-in diagnostics: evaluated cases, errors, timeouts (subset of errors),
  // fallback-token results, and results without a fitness comparison.
  std::vector<std::array<unsigned int, 5>> case_counts;
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
                                double penalty = 1.0);
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
