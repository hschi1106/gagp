#pragma once
#include "gagp/core/bytecode.hpp"
#include <vector>
#include <array>
namespace gagp::evo::repro { class NativePhasePopulation; }
namespace gagp::gpu_detail {
// Borrowed only during a synchronous evaluation. The only producer owns both
// vectors privately: cold native verification plus checked GPU phase lowering.
// No public factory, mutable accessor, serialized token or verified=true flag.
class OwnedGpuPrograms final {
 public:
  OwnedGpuPrograms(const OwnedGpuPrograms&) = delete;
  const std::vector<BytecodeProgram>& programs() const { return programs_; }
  const std::vector<int>& stack_bounds() const { return stack_bounds_; }
  bool views_supported_for(const std::array<ValueTag,64>& types) const { return input_types_ && *input_types_==types; }
  bool direct_root_supported() const { return input_types_ && direct_root_; }
 private:
  friend class gagp::evo::repro::NativePhasePopulation;
  OwnedGpuPrograms(const std::vector<BytecodeProgram>& programs,const std::vector<int>& bounds,
      const std::array<ValueTag,64>* input_types=nullptr,bool direct_root=false)
      : programs_(programs),stack_bounds_(bounds),input_types_(input_types),direct_root_(direct_root) {}
  const std::vector<BytecodeProgram>& programs_;
  const std::vector<int>& stack_bounds_;
  const std::array<ValueTag,64>* input_types_;
  bool direct_root_;
};
struct PackResult;
PackResult pack_owned_programs(const OwnedGpuPrograms& owned,int case_count,unsigned input_payload_mask);
} // namespace gagp::gpu_detail
