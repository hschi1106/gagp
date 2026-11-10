#pragma once
#include <cuda_runtime.h>
#include <cstddef>

namespace gagp::gpu_detail {
// Device allocations. While a reproduction stage is active they come from the
// stage workspace (stage_storage.cu); otherwise from cudaMalloc, with freed
// blocks of at most 256 MiB cached for reuse (device_allocation.cu).
cudaError_t device_malloc(void** pointer,std::size_t bytes);
cudaError_t device_free(void* pointer);
// Returns cached free blocks to the driver, e.g. before sizing by free memory.
void release_cached_device_memory();
template<class T> inline cudaError_t device_malloc(T** pointer,std::size_t bytes) {
  return device_malloc(reinterpret_cast<void**>(pointer),bytes);
}
} // namespace gagp::gpu_detail
