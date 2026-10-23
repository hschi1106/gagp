#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

namespace gagp::gpu_detail {
namespace detail {struct CudaStageStorageState;}
struct CudaStageDebugStats {
  std::uint64_t banks_created=0,banks_alive=0,allocations=0,frees=0,live_allocations=0,failed_allocations=0,cleanup_errors=0;
};
// Private sequential-stage storage. The requested capacity is unchanged;
// another64KiB is allocator alignment overhead, not payload capacity.
class CudaStageStorage {
 public:
  explicit CudaStageStorage(std::size_t requested_capacity);
  ~CudaStageStorage();
  CudaStageStorage(const CudaStageStorage&)=delete;
  CudaStageStorage& operator=(const CudaStageStorage&)=delete;
  std::size_t capacity()const;
  std::size_t reserved_bytes()const;
  std::size_t peak_bytes()const;
  std::size_t live_allocations()const;
 private:
  std::shared_ptr<detail::CudaStageStorageState> state_;
  friend class CudaStageAllocationScope;
};
class CudaStageAllocationScope {
 public:
  // Reproduction routes ordinary temporary allocations into this bank.
  // Evaluation routes only explicit workspace allocations into it; metadata
  // and the persistent current population remain separate.
  CudaStageAllocationScope(const CudaStageStorage* storage,bool reproduction);
  ~CudaStageAllocationScope();
  CudaStageAllocationScope(const CudaStageAllocationScope&)=delete;
  CudaStageAllocationScope& operator=(const CudaStageAllocationScope&)=delete;
 private:
  struct Impl;std::unique_ptr<Impl> impl_;
};
CudaStageDebugStats cuda_stage_debug_stats();
} // namespace gagp::gpu_detail
