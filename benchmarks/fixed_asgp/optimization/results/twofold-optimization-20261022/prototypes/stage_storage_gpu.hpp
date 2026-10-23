#pragma once
#include <cuda_runtime.h>
#include <cstddef>

namespace gagp::gpu_detail {
bool cuda_stage_reproduction_active();
cudaError_t cuda_stage_workspace_malloc(void** pointer,std::size_t bytes);
bool cuda_stage_free_if_owned(void* pointer,cudaError_t* status);
} // namespace gagp::gpu_detail
