#include "../../src/runtime/gpu/cuda_stage_storage.hpp"
#include "../../src/runtime/gpu/stage_storage_gpu.hpp"
#include "gagp/runtime/gpu/allocation_gpu.hpp"
#include <cuda_runtime.h>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace gagp::gpu_detail;
void checked(cudaError_t result){if(result!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(result));}
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
__global__ void fill(int* out,int count,int seed){const int i=blockIdx.x*blockDim.x+threadIdx.x;if(i<count)out[i]=seed+31*i;}
void read_check(void* pointer,int seed) {
 std::vector<int> actual(1024);checked(cudaMemcpy(actual.data(),pointer,4096,cudaMemcpyDeviceToHost));
 for(int i=0;i<1024;++i)require(actual[i]==seed+31*i,"stage reuse corrupted initialized data");
}
void fill_check(void* pointer,int seed){
 require(reinterpret_cast<std::uintptr_t>(pointer)%256==0,"stage storage lost cudaMalloc alignment");
 fill<<<8,128>>>(static_cast<int*>(pointer),1024,seed);checked(cudaGetLastError());read_check(pointer,seed);
}

int main() try {
 checked(cudaSetDevice(0));const auto initial=cuda_stage_debug_stats();
 {
  void* persistent=nullptr;checked(device_malloc(&persistent,4096));fill_check(persistent,99);
  CudaStageStorage bank(4*1024*1024);
  require(bank.capacity()==4*1024*1024 && bank.reserved_bytes()==bank.capacity()+64*1024,"stage storage changed the requested capacity");
  void* first=nullptr;
  for(int cycle=0;cycle<100;++cycle) {
   {
    CudaStageAllocationScope evaluation(&bank,false);void *workspace=nullptr,*tile=nullptr,*metadata=nullptr;
    checked(cuda_stage_workspace_malloc(&workspace,bank.capacity()-4096));
    checked(cuda_stage_workspace_malloc(&tile,4096));
    if(!first)first=workspace;require(first==workspace,"stage bank did not reuse the same backing");
    fill_check(workspace,cycle);fill_check(tile,cycle+1);
    checked(device_malloc(&metadata,4096));
    require(bank.live_allocations()==2,"evaluation metadata leaked into the full workspace bank");
    fill_check(metadata,17);checked(device_free(metadata));
    void* impossible=reinterpret_cast<void*>(1);
    require(cuda_stage_workspace_malloc(&impossible,1)==cudaErrorMemoryAllocation && !impossible,"workspace budget overflow was hidden");
    checked(device_free(tile));checked(cuda_stage_workspace_malloc(&tile,4096));fill_check(tile,71);
    checked(device_free(workspace));checked(device_free(tile));
   }
   require(bank.live_allocations()==0,"workspace allocations escaped evaluation");
   {
    CudaStageAllocationScope reproduction(&bank,true);void *a=nullptr,*b=nullptr,*c=nullptr,*replacement=nullptr;
    checked(device_malloc(&a,1024*1024));checked(device_malloc(&b,1024*1024));checked(device_malloc(&c,1024*1024));
    checked(device_free(b));checked(device_malloc(&replacement,1024*1024));
    require(replacement==b,"non-LIFO scratch hole could not be reused");
    fill_check(a,cycle+11);fill_check(c,cycle+19);fill_check(replacement,cycle+29);
    bool overlap=false;try{CudaStageAllocationScope invalid(&bank,false);}catch(const std::logic_error&){overlap=true;}
    require(overlap,"two active stages aliased one backing");
    {CudaStageAllocationScope suspended(nullptr,false);void* metadata=nullptr;
     checked(device_malloc(&metadata,4096));require(bank.live_allocations()==3,"suspended constructor metadata entered scratch bank");
     fill_check(metadata,103);checked(device_free(metadata));}
    fill<<<8,128>>>(static_cast<int*>(c),1024,41);checked(cudaGetLastError());
    checked(device_free(c));void* queued=nullptr;checked(device_malloc(&queued,1024*1024));
    require(queued==c,"queued free did not permit same-stream reuse");fill_check(queued,53);
    checked(device_free(queued));checked(device_free(a));checked(device_free(replacement));
   }
   require(bank.live_allocations()==0,"reproduction scratch did not retire");
   read_check(persistent,99);
  }
  require(bank.peak_bytes()<=bank.reserved_bytes(),"stage bank exceeded its physical backing");
  checked(device_free(persistent));
 }
 // Escaped clients own the allocation state until their matching free.
 {
  auto bank=std::make_unique<CudaStageStorage>(1024*1024);void* escaped=nullptr;
  {CudaStageAllocationScope scope(bank.get(),false);checked(cuda_stage_workspace_malloc(&escaped,4096));fill_check(escaped,831);}
  bool rejected=false;try{CudaStageAllocationScope invalid(bank.get(),true);}catch(const std::logic_error&){rejected=true;}
  require(rejected,"new stage reused storage with a live prior client");
  bank.reset();read_check(escaped,831);checked(device_free(escaped));
 }
 const auto final=cuda_stage_debug_stats();
 require(final.banks_alive==initial.banks_alive && final.live_allocations==initial.live_allocations &&
     final.cleanup_errors==initial.cleanup_errors && final.allocations-final.frees==initial.allocations-initial.frees,
     "stage storage leaked backing, clients or cleanup errors");
 std::cout<<"stage storage PASS cycles=100 allocations="<<final.allocations-initial.allocations<<" intentional_capacity_failures="<<final.failed_allocations-initial.failed_allocations<<'\n';
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
