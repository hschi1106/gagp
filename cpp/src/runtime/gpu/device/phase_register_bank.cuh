#pragma once

// Included inside gpu_detail. Fixed field access keeps small proven VM stacks
// and locals eligible for scalar registers instead of dynamic local arrays.
template<unsigned Count> struct DPhaseArrayBank {
  long long values[Count];
  __device__ __forceinline__ long long& operator[](int index){return values[index];}
};
template<unsigned Count> struct DPhaseRegisterStorage;
template<> struct DPhaseRegisterStorage<4> {long long v0,v1,v2,v3;};
template<> struct DPhaseRegisterStorage<8>:DPhaseRegisterStorage<4> {long long v4,v5,v6,v7;};

template<unsigned Count> struct DPhaseRegisterBank:DPhaseRegisterStorage<Count> {
  static_assert(Count==4 || Count==8);
  __device__ __forceinline__ long long read(int index) const {
    switch(index) {
      case 0:return this->v0;case 1:return this->v1;
      case 2:return this->v2;case 3:return this->v3;
      default:break;
    }
    if constexpr(Count==8)switch(index) {
      case 4:return this->v4;case 5:return this->v5;
      case 6:return this->v6;case 7:return this->v7;
      default:break;
    }
    // Fresh stack/local proofs exclude this path. Detect a broken certificate
    // rather than silently substituting a value after an internal overrun.
    __trap();return 0;
  }
  __device__ __forceinline__ void write(int index,long long value) {
    switch(index) {
      case 0:this->v0=value;return;case 1:this->v1=value;return;
      case 2:this->v2=value;return;case 3:this->v3=value;return;
      default:break;
    }
    if constexpr(Count==8)switch(index) {
      case 4:this->v4=value;return;case 5:this->v5=value;return;
      case 6:this->v6=value;return;case 7:this->v7=value;return;
      default:break;
    }
    __trap();
  }
  struct Reference {
    DPhaseRegisterBank& bank;int index;
    __device__ __forceinline__ Reference(DPhaseRegisterBank& b,int i):bank(b),index(i){}
    __device__ __forceinline__ Reference(const Reference& other)=default;
    __device__ __forceinline__ operator long long() const{return bank.read(index);}
    __device__ __forceinline__ Reference& operator=(long long value){bank.write(index,value);return *this;}
    __device__ __forceinline__ Reference& operator=(const Reference& other){return *this=static_cast<long long>(other);}
  };
  __device__ __forceinline__ Reference operator[](int index){return Reference(*this,index);}
};
