#include "cuda_allocation_scope.hpp"
#include "stage_storage_gpu.hpp"
#include "gagp/runtime/gpu/allocation_gpu.hpp"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace gagp::gpu_detail {
namespace {
struct Counters {
  std::atomic<std::uint64_t> created{0},alive{0},allocations{0},frees{0},live{0},cleanup_errors{0};
} counters;
void note_cleanup(cudaError_t error) {if(error!=cudaSuccess)++counters.cleanup_errors;}
cudaError_t first_error(cudaError_t a,cudaError_t b) {return a==cudaSuccess?b:a;}
struct DeviceGuard {
  int previous=-1;bool changed=false;cudaError_t status;
  explicit DeviceGuard(int device):status(cudaGetDevice(&previous)) {
    if(status==cudaSuccess && previous!=device){status=cudaSetDevice(device);changed=status==cudaSuccess;}
  }
  ~DeviceGuard(){if(changed)note_cleanup(cudaSetDevice(previous));}
};
struct Pool {
  int device=-1;cudaMemPool_t handle=nullptr;std::atomic<bool> open{true};bool counted=false;
  cudaError_t shutdown() {
    if(!handle)return cudaSuccess;
    DeviceGuard selected(device);if(selected.status!=cudaSuccess)return selected.status;
    auto error=cudaStreamSynchronize(nullptr);
    const auto destroyed=cudaMemPoolDestroy(handle);error=first_error(error,destroyed);
    if(destroyed==cudaSuccess){handle=nullptr;if(counted){--counters.alive;counted=false;}}
    return error;
  }
  ~Pool(){note_cleanup(shutdown());}
};
std::mutex allocation_mutex;
std::unordered_map<void*,std::shared_ptr<Pool>> allocations;
bool requested() {
  const auto* value=std::getenv("GAGP_GPU_CUDA_POOL");
  if(!value || std::string(value)=="0")return false;
  if(std::string(value)!="1")throw std::runtime_error("GAGP_GPU_CUDA_POOL must be0 or1");
  return true;
}
} // namespace

namespace detail {
struct CudaAllocationScopeState;
thread_local CudaAllocationScopeState* current_scope=nullptr;
struct CudaAllocationScopeState {
  CudaAllocationScopeState* previous=current_scope;bool closed=false;std::uint64_t calls=0;
  std::vector<std::shared_ptr<Pool>> pools;
  CudaAllocationScopeState(){current_scope=this;}
  cudaError_t pool_for(int device,std::shared_ptr<Pool>& result) {
    for(const auto& pool:pools)if(pool->device==device){result=pool;return cudaSuccess;}
    int supported=0;auto error=cudaDeviceGetAttribute(&supported,cudaDevAttrMemoryPoolsSupported,device);
    if(error!=cudaSuccess)return error;
    if(!supported)return cudaErrorNotSupported;
    auto pool=std::make_shared<Pool>();pool->device=device;
    cudaMemPoolProps properties{};properties.allocType=cudaMemAllocationTypePinned;
    properties.handleTypes=cudaMemHandleTypeNone;properties.location.type=cudaMemLocationTypeDevice;
    properties.location.id=device;
    error=cudaMemPoolCreate(&pool->handle,&properties);if(error!=cudaSuccess)return error;
    // This retains unused backing; it is not a limit on live allocations or
    // a replacement for the independent16GiB evaluation-workspace cap.
    std::uint64_t retention=UINT64_C(16)*1024*1024*1024;
    error=cudaMemPoolSetAttribute(pool->handle,cudaMemPoolAttrReleaseThreshold,&retention);
    if(error!=cudaSuccess)return error;
    pool->counted=true;++counters.created;++counters.alive;
    pools.push_back(pool);result=std::move(pool);return cudaSuccess;
  }
  cudaError_t close() {
    if(closed)return cudaSuccess;
    if(current_scope!=this)return cudaErrorInvalidValue;
    current_scope=previous;closed=true;cudaError_t error=cudaSuccess;
    for(auto& pool:pools) {
      pool->open=false;DeviceGuard selected(pool->device);
      if(selected.status!=cudaSuccess){error=first_error(error,selected.status);continue;}
      error=first_error(error,cudaStreamSynchronize(nullptr));
      error=first_error(error,cudaMemPoolTrimTo(pool->handle,0));
      // Escaped session workspace keeps a shared owner. Never free its memory
      // just because the native genotype owner's lexical scope has ended.
      if(pool.use_count()==1)error=first_error(error,pool->shutdown());
    }
    pools.clear();return error;
  }
  ~CudaAllocationScopeState(){note_cleanup(close());}
};
} // namespace detail

CudaAllocationScope::CudaAllocationScope():CudaAllocationScope(requested()) {}
CudaAllocationScope::CudaAllocationScope(bool enabled) {
  if(enabled)state_=std::make_unique<detail::CudaAllocationScopeState>();
}
CudaAllocationScope::~CudaAllocationScope()=default;
bool CudaAllocationScope::used() const {return state_ && state_->calls;}
void CudaAllocationScope::finish() {
  if(state_) {
    const auto error=state_->close();
    if(error!=cudaSuccess)throw std::runtime_error(std::string("CUDA allocation pool cleanup: ")+cudaGetErrorString(error));
  }
}
CudaPoolDebugStats cuda_pool_debug_stats() {
  return {counters.created.load(),counters.alive.load(),counters.allocations.load(),counters.frees.load(),
          counters.live.load(),counters.cleanup_errors.load()};
}
cudaError_t cuda_pool_malloc(void** pointer,std::size_t bytes) {
  if(cuda_stage_reproduction_active())return cuda_stage_workspace_malloc(pointer,bytes);
  auto* scope=detail::current_scope;
  if(!scope)return cudaMalloc(pointer,bytes);
  if(!pointer)return cudaErrorInvalidValue;
  int device=-1;auto error=cudaGetDevice(&device);if(error!=cudaSuccess)return error;
  std::shared_ptr<Pool> pool;
  try {error=scope->pool_for(device,pool);}catch(const std::bad_alloc&){return cudaErrorMemoryAllocation;}
  if(error!=cudaSuccess)return error;
  error=cudaMallocFromPoolAsync(pointer,bytes,pool->handle,nullptr);if(error!=cudaSuccess)return error;
  if(!*pointer)return cudaSuccess;
  try {
    std::lock_guard<std::mutex> lock(allocation_mutex);
    if(!allocations.emplace(*pointer,pool).second)return cudaErrorUnknown;
  }catch(const std::bad_alloc&) {
    const auto freed=cudaFreeAsync(*pointer,nullptr);note_cleanup(freed);
    note_cleanup(cudaStreamSynchronize(nullptr));*pointer=nullptr;return cudaErrorMemoryAllocation;
  }
  ++scope->calls;++counters.allocations;++counters.live;return cudaSuccess;
}
cudaError_t cuda_pool_free(void* pointer) {
  cudaError_t stage_status;
  if(cuda_stage_free_if_owned(pointer,&stage_status))return stage_status;
  std::shared_ptr<Pool> pool;bool legacy=false;
  {
    std::lock_guard<std::mutex> lock(allocation_mutex);
    const auto found=allocations.find(pointer);
    if(found==allocations.end())legacy=true;
    else {
      pool=found->second;DeviceGuard selected(pool->device);if(selected.status!=cudaSuccess)return selected.status;
      const auto error=cudaFreeAsync(pointer,nullptr);if(error!=cudaSuccess)return error;
      allocations.erase(found);++counters.frees;--counters.live;
    }
  }
  if(legacy)return cudaFree(pointer);
  if(!pool->open.load()) {
    DeviceGuard selected(pool->device);if(selected.status!=cudaSuccess)return selected.status;
    auto error=cudaStreamSynchronize(nullptr);
    // Late frees must release unused backing even if another escaped session
    // allocation still retains the pool. No retained pool survives last free.
    error=first_error(error,cudaMemPoolTrimTo(pool->handle,0));
    if(pool.use_count()==1)error=first_error(error,pool->shutdown());
    return error;
  }
  return cudaSuccess;
}
} // namespace gagp::gpu_detail
