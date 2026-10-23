#include "cuda_stage_storage.hpp"
#include "stage_storage_gpu.hpp"
#include "gagp/runtime/gpu/allocation_gpu.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace gagp::gpu_detail {
namespace {
struct Counts {std::atomic<std::uint64_t> created{0},alive{0},allocations{0},frees{0},live{0},failed{0},cleanup{0};} counts;
void cleanup(cudaError_t result){if(result!=cudaSuccess)++counts.cleanup;}
void checked(cudaError_t result){if(result!=cudaSuccess)throw std::runtime_error(std::string("CUDA stage storage: ")+cudaGetErrorString(result));}
constexpr std::size_t alignment=256,alignment_overhead=64*1024;
}
namespace detail {
struct CudaStageStorageState {
  struct Extent {std::size_t offset,bytes;};
  void* base=nullptr;std::size_t budget=0,reserved=0,live_bytes=0,live_reserved=0,live=0,peak=0;int device=-1;
  std::vector<Extent> free_extents;
  bool busy=false;std::mutex mutex;
  explicit CudaStageStorageState(std::size_t bytes):budget(bytes) {
    if(!bytes || bytes>std::numeric_limits<std::size_t>::max()-alignment_overhead)
      throw std::invalid_argument("invalid CUDA stage storage capacity");
    reserved=bytes+alignment_overhead;free_extents.reserve(258);free_extents.push_back({0,reserved});
    checked(cudaGetDevice(&device));
    checked(cudaMalloc(&base,reserved));++counts.created;++counts.alive;
  }
  ~CudaStageStorageState() {
    int previous=-1;const auto queried=cudaGetDevice(&previous);cleanup(queried);
    const auto selected=queried==cudaSuccess && previous!=device?cudaSetDevice(device):queried;
    cleanup(selected);
    if(selected==cudaSuccess){cleanup(cudaStreamSynchronize(nullptr));cleanup(cudaFree(base));}
    if(previous>=0 && previous!=device)cleanup(cudaSetDevice(previous));
    --counts.alive;
  }
};
thread_local std::shared_ptr<CudaStageStorageState> active_stage;
thread_local bool reproduction_stage=false;
}
namespace {
struct Allocation {std::shared_ptr<detail::CudaStageStorageState> owner;std::size_t bytes,offset,span;};
std::mutex registry_mutex;
std::unordered_map<void*,Allocation> registry;
cudaError_t allocate(const std::shared_ptr<detail::CudaStageStorageState>& bank,void** pointer,std::size_t bytes) {
  if(!pointer)return cudaErrorInvalidValue;
  *pointer=nullptr;if(!bytes)return cudaSuccess;
  std::lock_guard<std::mutex> registration(registry_mutex);
  std::lock_guard<std::mutex> ownership(bank->mutex);
  if(bytes>bank->budget-bank->live_bytes || bank->live>=256) {
    ++counts.failed;return cudaErrorMemoryAllocation;
  }
  const auto span=(bytes+alignment-1)&~(alignment-1);
  auto found=std::find_if(bank->free_extents.begin(),bank->free_extents.end(),[&](auto extent){return extent.bytes>=span;});
  if(found==bank->free_extents.end()){++counts.failed;return cudaErrorMemoryAllocation;}
  const auto start=found->offset;
  auto* result=static_cast<char*>(bank->base)+start;
  try {
    if(!registry.emplace(result,Allocation{bank,bytes,start,span}).second)return cudaErrorUnknown;
  }catch(const std::bad_alloc&){++counts.failed;return cudaErrorMemoryAllocation;}
  found->offset+=span;found->bytes-=span;if(!found->bytes)bank->free_extents.erase(found);
  bank->live_bytes+=bytes;bank->live_reserved+=span;++bank->live;
  bank->peak=std::max(bank->peak,bank->live_reserved);++counts.allocations;++counts.live;
  *pointer=result;return cudaSuccess;
}
}

CudaStageStorage::CudaStageStorage(std::size_t bytes):state_(std::make_shared<detail::CudaStageStorageState>(bytes)){}
CudaStageStorage::~CudaStageStorage()=default;
std::size_t CudaStageStorage::capacity()const{return state_->budget;}
std::size_t CudaStageStorage::reserved_bytes()const{return state_->reserved;}
std::size_t CudaStageStorage::peak_bytes()const{std::lock_guard<std::mutex> lock(state_->mutex);return state_->peak;}
std::size_t CudaStageStorage::live_allocations()const{std::lock_guard<std::mutex> lock(state_->mutex);return state_->live;}
struct CudaStageAllocationScope::Impl {
  std::shared_ptr<detail::CudaStageStorageState> active,previous;
  bool previous_reproduction;
  explicit Impl(std::shared_ptr<detail::CudaStageStorageState> value,bool reproduction):
      active(std::move(value)),previous(detail::active_stage),previous_reproduction(detail::reproduction_stage) {
    std::lock_guard<std::mutex> lock(active->mutex);
    if(active->busy || active->live)throw std::logic_error("CUDA stage storage has an active stage or unreleased scratch");
    active->busy=true;detail::active_stage=active;detail::reproduction_stage=reproduction;
  }
  ~Impl(){
    detail::active_stage=previous;detail::reproduction_stage=previous_reproduction;
    std::lock_guard<std::mutex> lock(active->mutex);active->busy=false;
  }
};
CudaStageAllocationScope::CudaStageAllocationScope(const CudaStageStorage* storage,bool reproduction) {
  if(storage)impl_=std::make_unique<Impl>(storage->state_,reproduction);
}
CudaStageAllocationScope::~CudaStageAllocationScope()=default;
bool cuda_stage_reproduction_active(){return detail::active_stage && detail::reproduction_stage;}
cudaError_t cuda_stage_workspace_malloc(void** pointer,std::size_t bytes) {
  if(!detail::active_stage)return cuda_pool_malloc(pointer,bytes);
  return allocate(detail::active_stage,pointer,bytes);
}
bool cuda_stage_free_if_owned(void* pointer,cudaError_t* status) {
  std::shared_ptr<detail::CudaStageStorageState> owner;
  {
    std::lock_guard<std::mutex> registration(registry_mutex);
    const auto found=registry.find(pointer);if(found==registry.end())return false;
    owner=found->second.owner;std::lock_guard<std::mutex> ownership(owner->mutex);
    const auto bytes=found->second.bytes,offset=found->second.offset,span=found->second.span;registry.erase(found);
    owner->live_bytes-=bytes;--owner->live;
    owner->live_reserved-=span;owner->free_extents.push_back({offset,span});
    std::sort(owner->free_extents.begin(),owner->free_extents.end(),[](auto a,auto b){return a.offset<b.offset;});
    std::size_t out=0;
    for(const auto extent:owner->free_extents) {
      if(out && owner->free_extents[out-1].offset+owner->free_extents[out-1].bytes==extent.offset)
        owner->free_extents[out-1].bytes+=extent.bytes;
      else owner->free_extents[out++]=extent;
    }
    owner->free_extents.resize(out);
    ++counts.frees;--counts.live;
  }
  // All native operations use the existing default stream, so reuse follows
  // earlier kernels/copies without an extra implicit device synchronization.
  *status=cudaSuccess;return true;
}
CudaStageDebugStats cuda_stage_debug_stats(){return {counts.created.load(),counts.alive.load(),counts.allocations.load(),counts.frees.load(),
    counts.live.load(),counts.failed.load(),counts.cleanup.load()};}
} // namespace gagp::gpu_detail
