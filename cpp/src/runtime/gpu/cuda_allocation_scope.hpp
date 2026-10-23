#pragma once
#include <cstdint>
#include <memory>

namespace gagp::gpu_detail {
namespace detail {struct CudaAllocationScopeState;}
struct CudaPoolDebugStats {
  std::uint64_t pools_created=0,pools_alive=0,allocations=0,frees=0,live_allocations=0,cleanup_errors=0;
};
CudaPoolDebugStats cuda_pool_debug_stats();
// Private native/default-stream path only. No CUDA headers in the C++ caller.
class CudaAllocationScope final {
 public:
  CudaAllocationScope();
  explicit CudaAllocationScope(bool enabled);
  ~CudaAllocationScope();
  CudaAllocationScope(const CudaAllocationScope&)=delete;
  CudaAllocationScope& operator=(const CudaAllocationScope&)=delete;
  bool used() const;
  void finish(); // Synchronize/trim and report errors before normal return.
 private:
  std::unique_ptr<detail::CudaAllocationScopeState> state_;
};
} // namespace gagp::gpu_detail
