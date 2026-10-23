"""Exact cross-parent lookup; one-shot edits while holding the common GPU lock."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[6]

HEADER = r'''#pragma once
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
'''


def apply(dry_run=False):
    changes = {}

    def edit(name, old, new):
        path = ROOT / name
        text = changes.get(path, path.read_text())
        assert text.count(old) == 1, (name, old[:100], text.count(old))
        changes[path] = text.replace(old, new)

    header = ROOT / 'cpp/src/evolution/repro/gpu/parent_fitness_cache.cuh'
    assert not header.exists()
    changes[header] = HEADER
    source = 'cpp/src/evolution/repro/gpu/phase_population.cu'
    edit(source, '#include "completed_fitness_reuse.hpp"',
         '#include "completed_fitness_reuse.hpp"\n#include "parent_fitness_cache.cuh"')
    marker = 'template<bool SharedNodes, bool CompactFrames = false>\n__global__ void compile_population'
    kernels = r'''// Index the previous generation before stage-storage overwrites its trees.
// Separate launches ensure all hash/table writes finish before offspring lookup.
__global__ void index_fitness_parents(TreePointers parents,int population,int slots,
    std::uint64_t* hashes,int* table,int mask) {
 const int parent=blockIdx.x*blockDim.x+threadIdx.x;if(parent>=population)return;
 const auto hash=parent_cache::hash_program(parents,parent,slots);hashes[parent]=hash;
 const auto first=static_cast<unsigned>(hash)&static_cast<unsigned>(mask);
 for(unsigned probe=0;probe<64;++probe) {
  const int previous=atomicCAS(table+((first+probe)&mask),-1,parent);
  if(previous<0 || parent_cache::equal_program(parents,parent,parents,previous,slots))return;
 }
}
__global__ void find_fitness_parents(TreePointers parents,TreePointers children,int population,int slots,
    const std::uint64_t* hashes,const int* table,int mask,OperatorStats* stats) {
 const int child=blockIdx.x*blockDim.x+threadIdx.x;
 if(child>=population || stats[child].error || stats[child].reusable_parent>=0)return;
 const auto hash=parent_cache::hash_program(children,child,slots);
 stats[child].reusable_parent=parent_cache::find_parent(parents,children,child,slots,hashes,table,mask,hash);
}
'''
    edit(source, marker, kernels + marker)
    edit(source, ' std::size_t peak_transport_bytes=0;', ''' const bool parent_fitness_cache=fitness_reuse.enabled() && std::getenv("GAGP_GPU_PARENT_FITNESS_CACHE") &&
     std::string(std::getenv("GAGP_GPU_PARENT_FITNESS_CACHE"))=="1";
 Buffer<std::uint64_t> parent_hashes;Buffer<int> parent_table;int parent_table_size=0;
 std::size_t peak_transport_bytes=0;''')
    edit(source, '      constants.bytes()+packed_constants.bytes()+compiled.bytes()+code_offsets.bytes()+constant_offsets.bytes();',
         '      constants.bytes()+packed_constants.bytes()+compiled.bytes()+code_offsets.bytes()+constant_offsets.bytes()+parent_hashes.bytes()+parent_table.bytes();')
    edit(source, ' if(impl_->linear_elite)impl_->reproduction_profile+="-elite-linear";', ''' if(impl_->linear_elite)impl_->reproduction_profile+="-elite-linear";
 if(impl_->parent_fitness_cache) {
  phase_layout_require(impl_->population<(1<<29),"parent fitness cache population capacity");
  impl_->parent_table_size=1;while(impl_->parent_table_size<2*impl_->population)impl_->parent_table_size*=2;
  impl_->parent_hashes.reserve(impl_->population);impl_->parent_table.reserve(impl_->parent_table_size);
  impl_->reproduction_profile+="-parent-fitness-cache";
 }''')
    edit(source, ' if(impl_->stage_storage){copy_current_population<<<(p*k+127)/128,128>>>(pointers(*offspring),pointers(*impl_->current),p*k);checked(cudaGetLastError());}', ''' if(impl_->parent_fitness_cache) {
  checked(cudaMemset(impl_->parent_table.data,0xff,impl_->parent_table.bytes()));
  index_fitness_parents<<<(p+63)/64,64>>>(pointers(*impl_->current),p,k,
      impl_->parent_hashes.data,impl_->parent_table.data,impl_->parent_table_size-1);checked(cudaGetLastError());
  find_fitness_parents<<<(p+63)/64,64>>>(pointers(*impl_->current),pointers(*offspring),p,k,
      impl_->parent_hashes.data,impl_->parent_table.data,impl_->parent_table_size-1,impl_->stats.data);checked(cudaGetLastError());
 }
 if(impl_->stage_storage){copy_current_population<<<(p*k+127)/128,128>>>(pointers(*offspring),pointers(*impl_->current),p*k);checked(cudaGetLastError());}''')
    edit('cpp/src/evolution/repro/gpu/completed_fitness_reuse.hpp',
         '// The GPU owner alone supplies parent maps after an exact post-elite genotype\n// comparison. No AST hash, public certificate or approximate equality is used.',
         '// The GPU owner supplies parent maps after exact post-elite genotype\n// comparison. An optional hash index finds other previous-generation candidates;\n// full equality remains mandatory. No public proof or approximate equality is used.')

    test = 'cpp/tests/gpu/test_gpu_phase_population.cu'
    edit(test, '#include "../../src/evolution/repro/gpu/phase_population.hpp"',
         '#include "../../src/evolution/repro/gpu/phase_population.hpp"\n#include "../../src/evolution/repro/gpu/parent_fitness_cache.cuh"')
    edit(test, '    const std::vector<CaseBindings>& cases,const std::vector<Value>& answers) {\n const char* flag="GAGP_GPU_UNCHANGED_FITNESS_REUSE";',
         '''    const std::vector<CaseBindings>& cases,const std::vector<Value>& answers,bool cross_parent=false) {
 const char* cache_flag="GAGP_GPU_PARENT_FITNESS_CACHE";
 const auto* cache_prior=std::getenv(cache_flag);const bool cache_present=cache_prior;
 const std::string cache_saved=cache_prior?cache_prior:"";unsetenv(cache_flag);
 const char* flag="GAGP_GPU_UNCHANGED_FITNESS_REUSE";''')
    edit(test, ' setenv(flag,"1",1);repro::NativePhasePopulation cached(grammar,parents,names);', ''' setenv(flag,"1",1);if(cross_parent)setenv(cache_flag,"1",1);
 repro::NativePhasePopulation cached(grammar,parents,names);
 require((std::string(cached.profile_name()).find("-parent-fitness-cache")!=std::string::npos)==cross_parent,
     "parent fitness index policy was not retained");
 if(cache_present)setenv(cache_flag,cache_saved.c_str(),1);else unsetenv(cache_flag);''')
    edit(test, ' check_unchanged_fitness_reuse(grammar,parents,names,cases,answers);', ''' check_unchanged_fitness_reuse(grammar,parents,names,cases,answers);
 check_unchanged_fitness_reuse(grammar,parents,names,cases,answers,true);''')
    probe = (Path(__file__).parent / 'parent_cache_probe.txt').read_text()
    edit(test, 'void check_pool_evolution(', probe + '\nvoid check_pool_evolution(')
    edit(test, ' check_linear_elite_evolution(grammar,parents,names,cases,answers);',
         ' check_parent_cache_keys();\n check_linear_elite_evolution(grammar,parents,names,cases,answers);')
    if not dry_run:
        for path,text in changes.items():
            path.write_text(text)
    return [str(p.relative_to(ROOT)) for p in changes]
