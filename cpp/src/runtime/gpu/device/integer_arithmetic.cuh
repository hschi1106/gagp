#pragma once

#include <climits>
#include "gagp/core/value_semantics.hpp"

namespace gagp::gpu_detail {

__device__ inline bool d_signed32_pair(long long a,long long b) {
  return a>=INT_MIN && a<=INT_MAX && b>=INT_MIN && b<=INT_MAX;
}

// The narrow instruction is used only after proving BOTH operands fit. The
// original signed64 behavior remains for every other pair, including overflow.
__device__ inline long long d_trunc_int_div0(long long a,long long b) {
  if(b==0)return 0;
  if(b==1)return a;
  if(b==-1)return a==INT64_MIN?0:vm_semantics::wrap_int_neg(a);
  if(b==2 || b==-2) {
    const auto magnitude=a<0?0ULL-static_cast<unsigned long long>(a):static_cast<unsigned long long>(a);
    const auto half=static_cast<long long>(magnitude>>1);
    return (a<0)!=(b<0)?-half:half;
  }
  if(d_signed32_pair(a,b))return static_cast<int>(a)/static_cast<int>(b);
  return a/b;
}

__device__ inline long long d_trunc_int_mod0(long long a,long long b) {
  if(b==0 || b==1 || b==-1)return 0;
  if(b==2 || b==-2) {
    const long long odd=static_cast<unsigned long long>(a)&1ULL;
    return a<0?-odd:odd;
  }
  if(d_signed32_pair(a,b))return static_cast<int>(a)%static_cast<int>(b);
  return a%b;
}

__device__ inline long long d_wrap_int_div(long long a,long long b) {
  return b==-1?vm_semantics::wrap_int_neg(a):d_trunc_int_div0(a,b);
}

// Callers retain their ordinary zero-divisor error/protection rule.
__device__ inline long long d_py_int_mod(long long a,long long b) {
  long long r=d_trunc_int_mod0(a,b);
  if(r && ((r<0)!=(b<0)))r+=b;
  return r;
}

} // namespace gagp::gpu_detail
