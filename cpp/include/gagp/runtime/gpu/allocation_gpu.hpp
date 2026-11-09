#pragma once
#include <cuda_runtime.h>
#include <cstddef>

namespace gagp::gpu_detail {
// Device allocations. While a reproduction stage is active they come from the
// stage workspace (stage_storage.cu); otherwise from cudaMalloc.
cudaError_t device_malloc(void** pointer,std::size_t bytes);
cudaError_t device_free(void* pointer);
template<class T> inline cudaError_t device_malloc(T** pointer,std::size_t bytes) {
  return device_malloc(reinterpret_cast<void**>(pointer),bytes);
}
} // namespace gagp::gpu_detail
