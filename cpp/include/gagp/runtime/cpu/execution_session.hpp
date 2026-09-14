#pragma once

#include <cstddef>
#include <memory>

#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"

namespace gagp {

// Owns an immutable bytecode snapshot and reusable, thread-confined execution
// scratch. Segment validation remains lazy so unreachable malformed descriptors
// and instruction-fuel precedence behave like the one-shot execution API.
class CpuExecutionSession {
 public:
  explicit CpuExecutionSession(BytecodeProgram program);
  ~CpuExecutionSession();
  CpuExecutionSession(CpuExecutionSession&&) noexcept;
  CpuExecutionSession& operator=(CpuExecutionSession&&) noexcept;
  CpuExecutionSession(const CpuExecutionSession&) = delete;
  CpuExecutionSession& operator=(const CpuExecutionSession&) = delete;

  ExecResult execute(const std::vector<std::pair<int, Value>>& inputs,
                     int fuel = 10000);
  // Frame and memo backing storage only; excludes the owned program and phase
  // verifier results. Memo contents and frames reset for every region invocation.
  std::size_t retained_region_bytes() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace gagp
