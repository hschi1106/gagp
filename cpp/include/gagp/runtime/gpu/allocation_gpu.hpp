#pragma once
#include <cuda_runtime.h>
#include <cstddef>

namespace gagp::gpu_detail {
// Off-scope allocations use the original runtime API. Pooled pointers retain
// their owning pool until their matching free, including after scope exit.
cudaError_t cuda_pool_malloc(void** pointer,std::size_t bytes);
cudaError_t cuda_pool_free(void* pointer);
template<class T> inline cudaError_t cuda_pool_malloc(T** pointer,std::size_t bytes) {
  return cuda_pool_malloc(reinterpret_cast<void**>(pointer),bytes);
}
} // namespace gagp::gpu_detail
