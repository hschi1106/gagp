#pragma once

#include <memory>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "gagp/core/bytecode.hpp"
#include "gagp/core/region_executable.hpp"
#include "gagp/core/errors.hpp"
#include "gagp/core/value.hpp"
#include "gagp/runtime/cpu/fitness_cpu.hpp"

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
  FitnessEvalResult eval_programs(const std::vector<BytecodeProgram>& programs, bool capture_case_counts = false) const;
  FitnessEvalResult eval_executables(const std::vector<RegionExecutable>& programs, bool capture_case_counts = false) const;
  bool is_ready() const;

 private:
  FitnessEvalResult eval_impl(const std::vector<BytecodeProgram>& programs, bool capture_case_counts,
      const RegionExecutableBatch* owned) const;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace gagp
