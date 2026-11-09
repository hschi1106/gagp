#include "stage_storage_gpu.hpp"
#include "gagp/runtime/gpu/allocation_gpu.hpp"

namespace gagp::gpu_detail {
cudaError_t device_malloc(void** pointer,std::size_t bytes) {
  if(cuda_stage_reproduction_active())return cuda_stage_workspace_malloc(pointer,bytes);
  return cudaMalloc(pointer,bytes);
}
cudaError_t device_free(void* pointer) {
  cudaError_t stage_status;
  if(cuda_stage_free_if_owned(pointer,&stage_status))return stage_status;
  return cudaFree(pointer);
}
} // namespace gagp::gpu_detail
