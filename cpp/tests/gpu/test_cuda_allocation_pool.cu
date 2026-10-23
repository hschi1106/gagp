#include <cuda_runtime.h>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
#include "../../src/runtime/gpu/cuda_allocation_scope.hpp"
#include "gagp/runtime/gpu/allocation_gpu.hpp"

using namespace gagp::gpu_detail;
void checked(cudaError_t error){if(error!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(error));}
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
__global__ void fill(int* values,int count,int seed) {
  const int index=blockIdx.x*blockDim.x+threadIdx.x;if(index<count)values[index]=seed+index*17;
}
void fill_and_check(int* pointer,int count,int seed) {
  fill<<<(count+127)/128,128>>>(pointer,count,seed);checked(cudaGetLastError());
  std::vector<int> actual(count);checked(cudaMemcpy(actual.data(),pointer,count*sizeof(int),cudaMemcpyDeviceToHost));
  for(int i=0;i<count;++i)require(actual[i]==seed+i*17,"stream-ordered allocation lost initialized contents");
}
int main() try {
  checked(cudaSetDevice(0));int supported=0;
  checked(cudaDeviceGetAttribute(&supported,cudaDevAttrMemoryPoolsSupported,0));
  if(!supported){std::cout<<"CUDA memory pool unsupported\n";return 77;}
  cudaMemPool_t initial_pool=nullptr;checked(cudaDeviceGetMemPool(&initial_pool,0));
  const auto initial=cuda_pool_debug_stats();require(initial.live_allocations==0 && initial.pools_alive==0,"initial pool ownership not empty");
  int *escaped=nullptr,*second=nullptr;void* ordinary=nullptr;checked(cudaMalloc(&ordinary,1024));
  {
    CudaAllocationScope scope(true);checked(cuda_pool_free(ordinary));
    int* temporary=nullptr;checked(cuda_pool_malloc(&temporary,65536));fill_and_check(temporary,1000,7);
    checked(cuda_pool_free(temporary));
    checked(cuda_pool_malloc(&escaped,65536));fill_and_check(escaped,1000,19);
    checked(cuda_pool_malloc(&second,65536));fill_and_check(second,1000,-17);
    require(scope.used(),"enabled native pool did not record allocations");
    scope.finish();
    auto stats=cuda_pool_debug_stats();require(stats.live_allocations==2 && stats.pools_alive==1,"scope freed escaped session storage");
  }
  fill_and_check(escaped,1000,25);checked(cuda_pool_free(escaped));
  auto intermediate=cuda_pool_debug_stats();require(intermediate.live_allocations==1 && intermediate.pools_alive==1,"first late free destroyed another live allocation");
  fill_and_check(second,1000,37);checked(cuda_pool_free(second));
  require(cuda_pool_debug_stats().pools_alive==0 && cuda_pool_debug_stats().live_allocations==0,"last late free retained pool ownership");
  {
    CudaAllocationScope outer(true);int* a=nullptr;checked(cuda_pool_malloc(&a,4096));
    {
      CudaAllocationScope inner(true);int* b=nullptr;checked(cuda_pool_malloc(&b,4096));
      fill_and_check(b,100,31);checked(cuda_pool_free(b));inner.finish();
    }
    fill_and_check(a,100,71);checked(cuda_pool_free(a));
    int* c=nullptr;checked(cuda_pool_malloc(&c,4096));
    fill<<<1,128>>>(c,100,42);checked(cudaGetLastError());
    checked(cuda_pool_free(c)); // No extra synchronize before a queued free.
    checked(cuda_pool_malloc(&c,4096));fill_and_check(c,100,91);checked(cuda_pool_free(c));
    void* impossible=nullptr;const auto bad=cuda_pool_malloc(&impossible,std::numeric_limits<std::size_t>::max());
    require(bad!=cudaSuccess && impossible==nullptr,"allocation failure was hidden or left an owned pointer");
    (void)cudaGetLastError();outer.finish();
  }
  {
    CudaAllocationScope disabled(false);void* pointer=nullptr;checked(cuda_pool_malloc(&pointer,4096));
    require(!disabled.used(),"disabled allocator created a pool");checked(cuda_pool_free(pointer));
  }
  cudaMemPool_t final_pool=nullptr;checked(cudaDeviceGetMemPool(&final_pool,0));require(initial_pool==final_pool,"native optimization changed the device current pool");
  const auto final=cuda_pool_debug_stats();
  require(final.pools_alive==0 && final.live_allocations==0 && final.cleanup_errors==0,"pool cleanup failed or leaked tracked allocations");
  require(final.allocations==final.frees && final.allocations>0,"pool allocation/free accounting incomplete");
  std::cout<<"CUDA allocation pool lifetime/ordering checks=PASS allocations="<<final.allocations<<'\n';
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
