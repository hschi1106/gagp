#pragma once
#include <cuda_runtime.h>
#include <cstdint>

namespace gagp::evo::repro::parent_cache {
// Hashes only choose candidates. Every reuse requires complete equality of all
// genotype fields; probing saturation merely declines reuse. No padding bytes,
// approximate numeric comparisons, source AST hashes or public proofs are used.
__host__ __device__ inline std::uint64_t mix(std::uint64_t h,std::uint64_t x) {
 return (h^x)*UINT64_C(0x100000001b3);
}
template<class Trees>
__device__ std::uint64_t hash_program(Trees trees,int program,int slots) {
 std::uint64_t h=UINT64_C(0xcbf29ce484222325);
 for(int k=0;k<slots;++k) {
  const auto t=trees.view(program*slots+k);
  h=mix(mix(h,t.node_count),t.constant_count);
  for(int i=0;i<t.node_count;++i) {
   const auto n=t.nodes[i];h=mix(h,static_cast<unsigned>(n.kind));
   h=mix(h,static_cast<unsigned>(n.opcode));h=mix(h,n.operand);h=mix(h,n.arity);
   h=mix(h,n.fuel);h=mix(h,n.merge_fuel);h=mix(h,t.origins[i]);
  }
  for(int i=0;i<t.constant_count;++i) {
   const auto v=t.constants[i];h=mix(h,static_cast<unsigned>(v.tag));
   h=mix(h,static_cast<std::uint64_t>(v.i));h=mix(h,v.b);
  }
 }
 return h;
}
template<class A,class B>
__device__ bool equal_program(A a,int ai,B b,int bi,int slots) {
 for(int k=0;k<slots;++k) {
  const auto x=a.view(ai*slots+k),y=b.view(bi*slots+k);
  if(x.node_count!=y.node_count || x.constant_count!=y.constant_count)return false;
  for(int i=0;i<x.node_count;++i) {
   const auto u=x.nodes[i],v=y.nodes[i];
   if(u.kind!=v.kind || u.opcode!=v.opcode || u.operand!=v.operand || u.arity!=v.arity ||
      u.fuel!=v.fuel || u.merge_fuel!=v.merge_fuel || x.origins[i]!=y.origins[i])return false;
  }
  for(int i=0;i<x.constant_count;++i) {
   const auto u=x.constants[i],v=y.constants[i];
   if(u.tag!=v.tag || u.i!=v.i || u.b!=v.b)return false;
  }
 }
 return true;
}
template<class A,class B>
__device__ int find_parent(A parents,B children,int child,int slots,
    const std::uint64_t* hashes,const int* table,int mask,std::uint64_t hash) {
 const auto first=static_cast<unsigned>(hash)&static_cast<unsigned>(mask);
 for(unsigned probe=0;probe<64;++probe) {
  const int parent=table[(first+probe)&mask];if(parent<0)return -1;
  if(hashes[parent]==hash && equal_program(parents,parent,children,child,slots))return parent;
 }
 return -1;
}
} // namespace gagp::evo::repro::parent_cache
