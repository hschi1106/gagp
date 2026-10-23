#pragma once
#include <cuda_runtime.h>
#include <climits>
#include <cmath>

namespace gagp::evo::repro {
// One256-thread block, population>0, no NaN (the existing owner precondition).
// Matches the existing rank rule: highest double fitness, lowest source index.
// No floating arithmetic, RNG draw, CPU selection or approximate ranking.
__device__ __forceinline__ int linear_elite_index(const double* fitness,int population) {
  __shared__ double warp_values[8];
  __shared__ int warp_indices[8];
  const int lane=threadIdx.x&31,warp=threadIdx.x>>5;
  double best=-INFINITY;int index=INT_MAX;
  for(int candidate=threadIdx.x;candidate<population;candidate+=256) {
    const auto value=fitness[candidate];
    if(value>best || (value==best && candidate<index)){best=value;index=candidate;}
  }
  for(int delta=16;delta;delta/=2) {
    const auto other=__shfl_down_sync(0xffffffffu,best,delta);
    const int next=__shfl_down_sync(0xffffffffu,index,delta);
    if(lane+delta<32 && (other>best || (other==best && next<index))){best=other;index=next;}
  }
  if(lane==0){warp_values[warp]=best;warp_indices[warp]=index;}
  __syncthreads();
  if(warp==0) {
    best=lane<8?warp_values[lane]:-INFINITY;index=lane<8?warp_indices[lane]:INT_MAX;
    for(int delta=16;delta;delta/=2) {
      const auto other=__shfl_down_sync(0xffffffffu,best,delta);
      const int next=__shfl_down_sync(0xffffffffu,index,delta);
      if(lane+delta<32 && (other>best || (other==best && next<index))){best=other;index=next;}
    }
    if(lane==0)warp_indices[0]=index;
  }
  __syncthreads();return warp_indices[0];
}
} // namespace gagp::evo::repro
