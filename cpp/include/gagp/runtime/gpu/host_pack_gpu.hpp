#pragma once

#include <cuda_runtime.h>

#include <string>
#include <vector>

#include "gagp/core/bytecode.hpp"
#include "gagp/runtime/cpu/fitness_cpu.hpp"
#include "gagp/runtime/gpu/device_types_gpu.hpp"
#include "gagp/runtime/gpu/region_types_gpu.hpp"

namespace gagp::gpu_detail {

struct PackResult {
  std::vector<DProgramMeta> metas;
  std::vector<DInstr> all_code;
  std::vector<Value> all_consts;
  std::vector<DInstr> all_phase_code;
  std::vector<Value> all_phase_consts;
  std::vector<DRegionSegment> region_segments;
  std::vector<DRegionPhase> region_phases;
  std::vector<DRegionPhaseBinding> region_bindings;
  std::vector<Value> packed_case_local_vals;
  std::vector<unsigned char> packed_case_local_set;
  std::size_t total_cases = 0;
  std::size_t max_code_len = 0;
};

PackResult pack_programs_with_shared_case_count(const std::vector<BytecodeProgram>& programs,
                                                int shared_case_count,
                                                unsigned shared_input_payload_mask);
void pack_shared_cases_only(const std::vector<CaseBindings>& shared_cases,
                            std::vector<Value>* packed_case_local_vals,
                            std::vector<unsigned char>* packed_case_local_set);

struct DeviceArena {
  Value* d_consts = nullptr;
  DInstr* d_code = nullptr;
  Value* d_phase_consts = nullptr;
  DInstr* d_phase_code = nullptr;
  DRegionSegment* d_region_segments = nullptr;
  DRegionPhase* d_region_phases = nullptr;
  DRegionPhaseBinding* d_region_bindings = nullptr;
  DRegionFrame* d_region_frames = nullptr;
  std::int64_t* d_region_memo_keys = nullptr;
  Value* d_region_memo_values = nullptr;
  DProgramMeta* d_metas = nullptr;
  Value* d_shared_case_local_vals = nullptr;
  unsigned char* d_shared_case_local_set = nullptr;
  DResult* d_out = nullptr;
  Value* d_expected = nullptr;
  double* d_fitness = nullptr;
  DStringPayloadEntry* d_string_payload_entries = nullptr;
  char* d_string_payload_bytes = nullptr;
  DListPayloadEntry* d_list_payload_entries = nullptr;
  Value* d_list_payload_values = nullptr;

  DeviceArena() = default;
  DeviceArena(const DeviceArena&) = delete;
  DeviceArena& operator=(const DeviceArena&) = delete;
  ~DeviceArena();
};

template <typename T>
bool cuda_alloc_and_copy_in(const std::vector<T>& host, T** dev) {
  if (host.empty()) {
    *dev = nullptr;
    return true;
  }
  if (host.size() > static_cast<std::size_t>(-1) / sizeof(T)) {
    *dev = nullptr;
    return false;
  }
  if (cudaMalloc(reinterpret_cast<void**>(dev), sizeof(T) * host.size()) != cudaSuccess) {
    return false;
  }
  if (cudaMemcpy(*dev, host.data(), sizeof(T) * host.size(), cudaMemcpyHostToDevice) != cudaSuccess) {
    cudaFree(*dev);
    *dev = nullptr;
    return false;
  }
  return true;
}

}  // namespace gagp::gpu_detail
