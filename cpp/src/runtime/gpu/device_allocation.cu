#include "stage_storage_gpu.hpp"
#include "gagp/runtime/gpu/allocation_gpu.hpp"

#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace gagp::gpu_detail {
namespace {
// Evaluation allocates and frees the same transport buffers on every call.
// Freed blocks up to kMaxCachedBlock are kept per device in power-of-two
// buckets and handed out again, which removes the cudaMalloc/cudaFree pair
// (and the implicit device synchronization of cudaFree) from each evaluation.
// All work runs on the legacy default stream, so a reused block is only
// written after every earlier kernel that read it. Contents of a new
// allocation are unspecified either way.
constexpr std::size_t kMinBucket = std::size_t(4) << 10;
constexpr std::size_t kMaxCachedBlock = std::size_t(256) << 20;
constexpr std::size_t kMaxCachedFree = std::size_t(1) << 30;

struct Block { int device = -1; std::size_t bytes = 0; };
struct Cache {
  std::mutex mutex;
  std::unordered_map<void*, Block> live;
  std::map<std::pair<int, std::size_t>, std::vector<void*>> free;
  std::size_t free_bytes = 0;
};
Cache& cache() {
  static Cache* instance = new Cache;  // Never destroyed: the CUDA runtime may be gone at exit.
  return *instance;
}
std::size_t bucket(std::size_t bytes) {
  std::size_t size = kMinBucket;
  while (size < bytes) size <<= 1;
  return size;
}
void release_free_locked(Cache& c) {
  for (auto& entry : c.free)
    for (void* pointer : entry.second) cudaFree(pointer);
  c.free.clear();
  c.free_bytes = 0;
}
}  // namespace

cudaError_t device_malloc(void** pointer,std::size_t bytes) {
  if(cuda_stage_reproduction_active())return cuda_stage_workspace_malloc(pointer,bytes);
  if(bytes==0 || bytes>kMaxCachedBlock)return cudaMalloc(pointer,bytes);
  int device=-1;
  if(const auto status=cudaGetDevice(&device);status!=cudaSuccess)return status;
  const std::size_t size=bucket(bytes);
  auto& c=cache();
  std::lock_guard<std::mutex> lock(c.mutex);
  if(auto found=c.free.find({device,size});found!=c.free.end() && !found->second.empty()) {
    *pointer=found->second.back();found->second.pop_back();c.free_bytes-=size;
    c.live[*pointer]=Block{device,size};
    return cudaSuccess;
  }
  auto status=cudaMalloc(pointer,size);
  if(status==cudaErrorMemoryAllocation && c.free_bytes) {
    cudaGetLastError();release_free_locked(c);status=cudaMalloc(pointer,size);
  }
  if(status==cudaSuccess)c.live[*pointer]=Block{device,size};
  return status;
}
cudaError_t device_free(void* pointer) {
  cudaError_t stage_status;
  if(cuda_stage_free_if_owned(pointer,&stage_status))return stage_status;
  if(pointer) {
    auto& c=cache();
    std::lock_guard<std::mutex> lock(c.mutex);
    if(const auto found=c.live.find(pointer);found!=c.live.end()) {
      const auto block=found->second;c.live.erase(found);
      if(c.free_bytes+block.bytes>kMaxCachedFree)return cudaFree(pointer);
      c.free[{block.device,block.bytes}].push_back(pointer);c.free_bytes+=block.bytes;
      return cudaSuccess;
    }
  }
  return cudaFree(pointer);
}
void release_cached_device_memory() {
  auto& c=cache();
  std::lock_guard<std::mutex> lock(c.mutex);
  release_free_locked(c);
}
} // namespace gagp::gpu_detail
