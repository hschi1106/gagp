#pragma once

// Included inside gpu_detail. Fixed-size VM stack and local storage.
template<unsigned Count> struct DPhaseArrayBank {
  long long values[Count];
  __device__ __forceinline__ long long& operator[](int index){return values[index];}
};
