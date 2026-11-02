#include "../../../runtime/gpu/owned_programs.hpp"
#include "gagp/core/engine_profile.hpp"
#include "../../../runtime/gpu/view_profile.hpp"
#include "../../../runtime/gpu/payload_workspace_budget.hpp"
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "gagp/core/bytecode_verify.hpp"
#include "phase_population.hpp"
#include "completed_fitness_reuse.hpp"
#include "parent_fitness_cache.cuh"
#include "../../../runtime/gpu/cuda_stage_storage.hpp"
#include "linear_elite.cuh"
#include "phase_layout.hpp"
#include "phase_operators.cuh"
#include "phase_choice_tables.hpp"
#include "internal.hpp"
#include "gagp/runtime/gpu/host_pack_gpu.hpp"
#include <cuda_runtime.h>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <limits>

namespace gagp::evo::repro {
namespace {
using phase_ops::capacity;using phase_ops::code_capacity;
using phase_ops::TreePointers;using phase_ops::DeviceSlot;using phase_ops::OperatorStats;
using phase_ops::OperatorPolicy;using phase_ops::valid;using phase_ops::copy_tree;
bool flag_is_one(const char* name){const char* value=gagp::engine_env(name);return value && std::string(value)=="1";}
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); }
void checked(cudaError_t status) { if (status != cudaSuccess) throw std::runtime_error(cudaGetErrorString(status)); }
template<class T> struct Buffer {
 T* data=nullptr;std::size_t count=0;
 Buffer()=default;Buffer(const Buffer&)=delete;
 ~Buffer(){if(data)gpu_detail::cuda_pool_free(data);}
 void clear(){if(data)checked(gpu_detail::cuda_pool_free(data));data=nullptr;count=0;}
 void reserve(std::size_t size) {
  if(size<=count)return;
  if(size>std::numeric_limits<std::size_t>::max()/sizeof(T))throw std::overflow_error("GPU genotype allocation overflow");
  T* next=nullptr;checked(gpu_detail::cuda_pool_malloc(reinterpret_cast<void**>(&next),size*sizeof(T)));
  if(data)checked(gpu_detail::cuda_pool_free(data));data=next;count=size;
 }
 void upload(const std::vector<T>& input){reserve(input.size());if(!input.empty())checked(cudaMemcpy(data,input.data(),input.size()*sizeof(T),cudaMemcpyHostToDevice));}
 std::vector<T> read(std::size_t size)const {
  if(size>count)throw std::out_of_range("GPU genotype buffer read");std::vector<T> result(size);
  if(size)checked(cudaMemcpy(result.data(),data,size*sizeof(T),cudaMemcpyDeviceToHost));return result;
 }
 std::size_t bytes()const{return count*sizeof(T);}
};
struct UploadedProfile {
 Buffer<PhaseExpression> expressions;Buffer<PhaseProduction> productions;Buffer<PhaseNonterminal> nonterminals;
 Buffer<PhaseDomain> domains;Buffer<Value> values;Buffer<unsigned> minimum;
 Buffer<PhaseChoiceHeader> choice_headers;Buffer<unsigned> choice_values;Buffer<PhaseChoiceSet> choice_sets;
 Buffer<std::uint64_t> choice_thresholds;Buffer<int> choice_expressions;bool choice_tables=false;
 UploadedProfile(const HostPhaseGrammar& h,bool with_choice_tables) {
  expressions.upload(h.expressions);
  auto weights=h.productions;auto nts=h.nonterminals;
  if(gagp::engine_env("GAGP_GPU_STATIC_WEIGHTS"))normalize_static_phase_weights(h,nts,weights);
  productions.upload(weights);nonterminals.upload(nts);
  domains.upload(h.domains);values.upload(h.values);minimum.upload(h.minimum);
  if(with_choice_tables) {
   const auto t=build_phase_choice_tables(h,nts,weights);
   choice_headers.upload(t.headers);choice_values.upload(t.values);choice_sets.upload(t.sets);
   choice_thresholds.upload(t.thresholds);choice_expressions.upload(t.expressions);choice_tables=!t.headers.empty();
  }
 }
 PhaseGrammarView view(const HostPhaseGrammar& h)const{
  PhaseGrammarView v{expressions.data,int(h.expressions.size()),productions.data,int(h.productions.size()),
   nonterminals.data,int(h.nonterminals.size()),domains.data,int(h.domains.size()),values.data,int(h.values.size()),minimum.data,h.depth_count,h.logical_depth_count,h.site_depth};
  if(choice_tables){v.choice_headers=choice_headers.data;v.choice_values=choice_values.data;v.choice_sets=choice_sets.data;
   v.choice_thresholds=choice_thresholds.data;v.choice_expressions=choice_expressions.data;}
  return v;
 }
 std::size_t bytes()const{return expressions.bytes()+productions.bytes()+nonterminals.bytes()+domains.bytes()+values.bytes()+minimum.bytes()+
  choice_headers.bytes()+choice_values.bytes()+choice_sets.bytes()+choice_thresholds.bytes()+choice_expressions.bytes();}
};
struct Trees {
 Buffer<PhaseTreeNode> nodes;Buffer<int> origins;Buffer<PhaseGeneSite> sites;Buffer<Value> constants;Buffer<PhaseGenerationResult> lengths;
 explicit Trees(std::size_t count){nodes.reserve(count*capacity);origins.reserve(count*capacity);sites.reserve(count*capacity);constants.reserve(count*capacity);lengths.reserve(count);}
 std::size_t bytes()const{return nodes.bytes()+origins.bytes()+sites.bytes()+constants.bytes()+lengths.bytes();}
};
TreePointers pointers(Trees& t){return {t.nodes.data,t.origins.data,t.sites.data,t.constants.data,t.lengths.data};}
// Preserve elites entirely on device; strict source-index tie breaking is deterministic.
__global__ void preserve_elites(TreePointers source, TreePointers target,
    const double* fitness, int population, int slots, int elites) {
  const int candidate=blockIdx.x*blockDim.x+threadIdx.x;
  if(candidate>=population)return;
  int rank=0;
  for(int i=0;i<population;++i)
    if(fitness[i]>fitness[candidate] || (fitness[i]==fitness[candidate] && i<candidate))++rank;
  if(rank<elites)for(int k=0;k<slots;++k)copy_tree(source,candidate*slots+k,target,rank*slots+k);
}
// Exactly the same elite destination and source-index tie rule as rank-based
// preservation. One block finds the winner; slots copy disjoint target rows.
__global__ void preserve_one_elite(TreePointers source,TreePointers target,
    const double* fitness,int population,int slots) {
 const int winner=linear_elite_index(fitness,population);
 for(int k=threadIdx.x;k<slots;k+=blockDim.x)copy_tree(source,winner*slots+k,target,k);
}
__global__ void copy_current_population(TreePointers source,TreePointers target,int rows) {
 const int row=blockIdx.x*blockDim.x+threadIdx.x;
 if(row<rows)copy_tree(source,row,target,row);
}
__global__ void annotate_import(TreePointers trees,const DeviceSlot* slots,int rows,int slot_count) {
 const int row=blockIdx.x*blockDim.x+threadIdx.x;if(row>=rows)return;const auto& s=slots[row%slot_count];const auto t=trees.view(row);
 if(!valid(trees.lengths[row]) || !annotate_phase_tree(s.grammar,s.root,s.scope,t.nodes,t.origins,t.node_count,trees.output(row).sites) ||
     (s.grammar.site_depth && phase_site_depth(t.nodes,trees.output(row).sites,t.node_count)>s.logical_depth))
   trees.lengths[row].status=PhaseCompileStatus::Invalid;
}
__global__ void measure_site_metrics(TreePointers input,int population,int slots,unsigned* output) {
 const int p=blockIdx.x*blockDim.x+threadIdx.x;if(p>=population)return;
 unsigned count=0,depth=0;
 for(int k=0;k<slots;++k) {
  const auto t=input.view(p*slots+k);
  for(int i=0;i<t.node_count;++i)count+=t.sites[i].nonterminal>=0;
  const int height=phase_site_depth(t.nodes,t.sites,t.node_count);
  if(height<0){output[p*2]=output[p*2+1]=0;return;}
  depth=unsigned(height)>depth?unsigned(height):depth;
 }
 output[p*2]=count;output[p*2+1]=depth;
}
// Compact cold transport: offsets and lengths are host-validated before launch.
struct ImportOffset { std::size_t nodes, constants; };
__global__ void scatter_import(TreePointers trees, const ImportOffset* offsets,
    const PhaseTreeNode* nodes, const int* origins, const Value* constants) {
 const int row=blockIdx.x;const auto r=trees.lengths[row];const auto offset=offsets[row];
 const auto out=trees.output(row);
 for(int i=threadIdx.x;i<r.nodes;i+=blockDim.x){out.nodes[i]=nodes[offset.nodes+i];out.origins[i]=origins[offset.nodes+i];}
 for(int i=threadIdx.x;i<r.constants;i+=blockDim.x)out.constants[i]=constants[offset.constants+i];
}
__global__ void gather_export(TreePointers trees,const ImportOffset* offsets,
    PhaseTreeNode* nodes,int* origins,Value* constants) {
 const int row=blockIdx.x;const auto r=trees.lengths[row];const auto offset=offsets[row];
 const auto input=trees.view(row);
 for(int i=threadIdx.x;i<r.nodes;i+=blockDim.x){nodes[offset.nodes+i]=input.nodes[i];origins[offset.nodes+i]=input.origins[i];}
 for(int i=threadIdx.x;i<r.constants;i+=blockDim.x)constants[offset.constants+i]=input.constants[i];
}
// Operator kernels delegate to the shared per-child bodies in phase_operators.cuh.
// Warp kernels own one child per warp; lane 0 publishes the private statistics.
template<bool Warp> __device__ void publish(OperatorStats* target,const OperatorStats& stat) {
 if constexpr(Warp){__syncwarp();if((threadIdx.x&31)==0)*target=stat;}
 else *target=stat;
}
// `children` offspring rows draw their parents from `population` members.
template<bool Warp>
__global__ void crossover_population(TreePointers input,TreePointers output,const DeviceSlot* slots,
    int slot_count,int population,int max_variable_nodes,const double* fitness,int pressure,std::uint64_t seed,OperatorStats* stats,OperatorPolicy policy,
    int children) {
 const int child=(blockIdx.x*blockDim.x+threadIdx.x)/(Warp?32:1);if(child>=children)return;
 OperatorStats stat;
 phase_ops::crossover_child<Warp>(child,input,output,slots,slot_count,population,max_variable_nodes,fitness,pressure,seed,policy,stat);
 publish<Warp>(stats+child,stat);
}
template<bool InPlace,bool Fused,bool Warp>
__global__ void mutate_population(TreePointers input,TreePointers output,TreePointers scratch,const DeviceSlot* slots,
    int slot_count,int population,int max_variable_nodes,double probability,std::uint64_t seed,OperatorStats* stats,OperatorPolicy policy) {
 const int child=(blockIdx.x*blockDim.x+threadIdx.x)/(Warp?32:1);if(child>=population)return;
 OperatorStats stat=stats[child];
 phase_ops::mutate_child<InPlace,Fused,Warp>(child,input,output,scratch,slots,slot_count,max_variable_nodes,probability,seed,policy,stat);
 publish<Warp>(stats+child,stat);
}
__global__ void compare_offspring(TreePointers parents,TreePointers children,int population,int slot_count,OperatorStats* stats,int child_count) {
 const int child=blockIdx.x*blockDim.x+threadIdx.x;if(child>=child_count)return;
 OperatorStats stat=stats[child];phase_ops::compare_child(child,parents,children,population,slot_count,stat);stats[child]=stat;
}
// Index the previous generation before stage-storage overwrites its trees.
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
__global__ void find_fitness_parents(TreePointers parents,TreePointers children,int child_count,int slots,
    const std::uint64_t* hashes,const int* table,int mask,OperatorStats* stats) {
 const int child=blockIdx.x*blockDim.x+threadIdx.x;
 if(child>=child_count || stats[child].error || stats[child].reusable_parent>=0)return;
 const auto hash=parent_cache::hash_program(children,child,slots);
 stats[child].reusable_parent=parent_cache::find_parent(parents,children,child,slots,hashes,table,mask,hash);
}
template<bool SharedNodes, bool CompactFrames = false>
__global__ void compile_population(TreePointers trees,const DeviceSlot* slots,int slot_count,int rows,
    gpu_detail::DInstr* code,Value* constants,PhaseCompileResult* compiled) {
 const int row=blockIdx.x*blockDim.x+threadIdx.x;if(row>=rows)return;
 compiled[row]=phase_ops::compile_row<SharedNodes,CompactFrames>(trees,slots,slot_count,row,code,constants);
}
// One block scans all live rows in tiles. The former one-thread dependent
// global loads/stores made this metadata step approach a millisecond at P=1024.
__global__ void offsets_kernel(const PhaseCompileResult* compiled,int rows,int* code_offsets,int* constant_offsets) {
 __shared__ int code_warps[8],constant_warps[8],invalid;
 const int tid=threadIdx.x,lane=tid&31,warp=tid>>5;
 if(tid==0){code_offsets[0]=0;constant_offsets[0]=0;invalid=0;}
 __syncthreads();int code_base=0,constant_base=0;
 for(int base=0;base<rows;base+=256) {
  const int row=base+tid;int code=0,constants=0;
  if(row<rows){const auto r=compiled[row];if(r.status!=PhaseCompileStatus::Ok)atomicExch(&invalid,1);
   else {code=r.code_count;constants=r.constant_count;}}
  for(int delta=1;delta<32;delta*=2){const int c=__shfl_up_sync(0xffffffffu,code,delta),v=__shfl_up_sync(0xffffffffu,constants,delta);
   if(lane>=delta){code+=c;constants+=v;}}
  if(lane==31){code_warps[warp]=code;constant_warps[warp]=constants;}
  __syncthreads();
  int prior_code=0,prior_constants=0;
  for(int w=0;w<warp;++w){prior_code+=code_warps[w];prior_constants+=constant_warps[w];}
  if(row<rows){code_offsets[row+1]=code_base+prior_code+code;constant_offsets[row+1]=constant_base+prior_constants+constants;}
  for(int w=0;w<8;++w){code_base+=code_warps[w];constant_base+=constant_warps[w];}
  __syncthreads();
 }
 if(tid==0 && invalid){code_offsets[rows]=-1;constant_offsets[rows]=-1;}
}
__global__ void compact_code(const gpu_detail::DInstr* source_code,const Value* source_constants,const int* code_offsets,
    const int* constant_offsets,int rows,gpu_detail::DInstr* code,Value* constants) {
 const int row=blockIdx.x;if(row>=rows)return;
 for(int j=threadIdx.x;j<code_offsets[row+1]-code_offsets[row];j+=blockDim.x)code[code_offsets[row]+j]=source_code[std::size_t(row)*code_capacity+j];
 for(int j=threadIdx.x;j<constant_offsets[row+1]-constant_offsets[row];j+=blockDim.x)constants[constant_offsets[row]+j]=source_constants[std::size_t(row)*capacity+j];
}
// Steady state: copy each placed child's rows over its member's rows.
__global__ void place_children(TreePointers children,TreePointers members,const int* placements,int count,int slots) {
 const int item=blockIdx.x*blockDim.x+threadIdx.x;if(item>=count*slots)return;
 const int pair=item/slots,k=item%slots,member=placements[2*pair],child=placements[2*pair+1];
 copy_tree(children,child*slots+k,members,member*slots+k);
}
// Staged steady state: compact copies of one batch's child rows survive
// evaluation outside the stage bank (the bank serves the payload workspace).
__global__ void stash_children(TreePointers trees,const ImportOffset* offsets,PhaseTreeNode* nodes,int* origins,
    PhaseGeneSite* sites,Value* constants,PhaseGenerationResult* lengths) {
 const int row=blockIdx.x;const auto r=trees.lengths[row];if(threadIdx.x==0)lengths[row]=r;if(!valid(r))return;
 const auto offset=offsets[row];const auto input=trees.view(row);
 for(int i=threadIdx.x;i<r.nodes;i+=blockDim.x){nodes[offset.nodes+i]=input.nodes[i];origins[offset.nodes+i]=input.origins[i];sites[offset.nodes+i]=input.sites[i];}
 for(int i=threadIdx.x;i<r.constants;i+=blockDim.x)constants[offset.constants+i]=input.constants[i];
}
// Same result as place_children/copy_tree, reading the compact stash.
__global__ void place_stashed(TreePointers members,const int* placements,int slots,const ImportOffset* offsets,
    const PhaseTreeNode* nodes,const int* origins,const PhaseGeneSite* sites,const Value* constants,const PhaseGenerationResult* lengths) {
 const int pair=blockIdx.x/slots,k=blockIdx.x%slots,member=placements[2*pair]*slots+k,child=placements[2*pair+1]*slots+k;
 const auto r=lengths[child];
 if(valid(r)) {
  const auto offset=offsets[child];const auto out=members.output(member);
  for(int i=threadIdx.x;i<r.nodes;i+=blockDim.x){out.nodes[i]=nodes[offset.nodes+i];out.origins[i]=origins[offset.nodes+i];out.sites[i]=sites[offset.nodes+i];}
  for(int i=threadIdx.x;i<r.constants;i+=blockDim.x)out.constants[i]=constants[offset.constants+i];
 }
 if(threadIdx.x==0)members.lengths[member]=valid(r)?r:PhaseGenerationResult{};
}
} // namespace

struct NativePhasePopulation::Impl {
 std::shared_ptr<const grammar::CompiledGrammar> grammar;
 // Search semantics come from the caller's policy; the remaining fields are
 // exact engine variants captured once at construction.
 OperatorPolicy operators;
 const bool linear_elite=gagp::engine_env("GAGP_GPU_LINEAR_ELITE") && std::string(gagp::engine_env("GAGP_GPU_LINEAR_ELITE"))=="1";
 std::string reproduction_profile;
 const bool inplace_mutation;
 const bool fused_donor_sites;
 // Exact warp-per-child variation and threshold-table production draws.
 const bool warp_variation;
 const bool choice_tables;
 const bool compact_compile_frames;
 const bool constructive_export;
 CompletedFitnessReuse fitness_reuse{gagp::engine_env("GAGP_GPU_UNCHANGED_FITNESS_REUSE") &&
     std::string(gagp::engine_env("GAGP_GPU_UNCHANGED_FITNESS_REUSE"))=="1"};
 const bool parent_fitness_cache=fitness_reuse.enabled() && gagp::engine_env("GAGP_GPU_PARENT_FITNESS_CACHE") &&
     std::string(gagp::engine_env("GAGP_GPU_PARENT_FITNESS_CACHE"))=="1";
 Buffer<std::uint64_t> parent_hashes;Buffer<int> parent_table;int parent_table_size=0;
 std::size_t peak_transport_bytes=0;
 bool staged_buffers=false;
 NativePhaseLayout layout;int population=0,slot_count=0,device=-1;bool healthy=true;
 std::unique_ptr<gpu_detail::CudaStageStorage> stage_storage;
 std::vector<std::unique_ptr<UploadedProfile>> profiles;Buffer<DeviceSlot> slots;
 std::unique_ptr<Trees> current,next,crossed,scratch;
 Buffer<double> fitness;Buffer<OperatorStats> stats;Buffer<unsigned> metrics;
 Buffer<gpu_detail::DInstr> code,packed_code;Buffer<Value> constants,packed_constants;
 Buffer<PhaseCompileResult> compiled;Buffer<int> code_offsets,constant_offsets;
 std::array<ValueTag,64> input_types;bool views_proven=true,direct_root_proven=true;
 bool private_intlists_proven=gagp::engine_env("GAGP_GPU_PRIVATE_INTLIST_OFFSETS") &&
     std::string(gagp::engine_env("GAGP_GPU_PRIVATE_INTLIST_OFFSETS"))=="1";
 // Executable state of one program set: the population members, or the
 // children of one steady-state batch (rows 0..count-1 of a tree buffer).
 struct ProgramSet {
  int count=0;
  std::vector<BytecodeProgram> programs;
  std::vector<int> fixed_stack_bounds,stack_bounds;
  std::unique_ptr<gpu_detail::PackResult> host_packed;
  std::vector<int> packed_phase_indices,initial_phase_stack_bounds;
  std::size_t fixed_phase_code=0,fixed_phase_constants=0;
  bool programs_current=true;
 };
 ProgramSet members,batch;
 bool steady=false;Buffer<int> placements;
 // Staged owners: compact batch rows plus host node metrics (see stash_children).
 Buffer<PhaseTreeNode> stash_nodes;Buffer<int> stash_origins;Buffer<PhaseGeneSite> stash_sites;Buffer<Value> stash_constants;
 Buffer<PhaseGenerationResult> stash_lengths;Buffer<ImportOffset> stash_offsets;
 std::vector<unsigned> stash_site_metrics,stash_node_counts;
 std::size_t stash_bytes()const{return stash_nodes.bytes()+stash_origins.bytes()+stash_sites.bytes()+stash_constants.bytes()+stash_lengths.bytes()+stash_offsets.bytes();}
 cudaEvent_t start=nullptr,stop=nullptr;
 void initialize_host_packed(ProgramSet& set,const gpu_detail::OwnedGpuPrograms& initial) {
  auto& host_packed=set.host_packed;auto& initial_phase_stack_bounds=set.initial_phase_stack_bounds;
  auto& packed_phase_indices=set.packed_phase_indices;const auto& programs=set.programs;
  host_packed=std::make_unique<gpu_detail::PackResult>(gpu_detail::pack_owned_programs(initial,0,0));
  auto& out=*host_packed;std::vector<bool> dynamic(out.region_phases.size(),false);
  for(const auto& phase:out.region_phases)initial_phase_stack_bounds.push_back(phase.program.verified_stack_bound);
  for(int p=0;p<set.count;++p) {
   const auto& segment=out.region_segments.at(out.metas.at(p).region_offset);
   const auto& plan=programs[p].bounded_region_segments[0].plan;
   for(const auto& slot:layout.slots) {
    int index=-1;
    switch(bounded_region_phase_kind(plan,slot.ordinal)) {
     case RegionPhaseKind::BasePredicate:index=segment.base_predicate_phase;break;
     case RegionPhaseKind::BaseBody:index=segment.base_body_phase;break;
     case RegionPhaseKind::Combine:index=segment.combine_phase;break;
     case RegionPhaseKind::Boundary:index=segment.boundary_phase;break;
     case RegionPhaseKind::Preparation:index=segment.preparation_phases[bounded_region_preparation_ordinal(plan,slot.ordinal)];break;
     case RegionPhaseKind::Request:index=segment.request_expression_phases[slot.ordinal-2-plan.preparations.size()];break;
    }
    phase_layout_require(index>=0 && std::size_t(index)<dynamic.size() && !dynamic[index],"owned packed phase mapping");
    dynamic[index]=true;packed_phase_indices.push_back(index);
   }
  }
  // Fixed phase storage is copied once. Current variable phases follow it, so
  // replacement never retains old-generation code or invalidates fixed offsets.
  auto code=std::move(out.all_phase_code);auto values=std::move(out.all_phase_consts);
  out.all_phase_code.clear();out.all_phase_consts.clear();
  out.all_phase_code.reserve(code.size());out.all_phase_consts.reserve(values.size());
  for(bool variable:{false,true}) {
   for(std::size_t i=0;i<out.region_phases.size();++i)if(dynamic[i]==variable) {
    auto& m=out.region_phases[i].program;const auto old_code=m.code_offset,old_const=m.const_offset;
    m.code_offset=out.all_phase_code.size();m.const_offset=out.all_phase_consts.size();
    out.all_phase_code.insert(out.all_phase_code.end(),code.begin()+old_code,code.begin()+old_code+m.code_len);
    out.all_phase_consts.insert(out.all_phase_consts.end(),values.begin()+old_const,values.begin()+old_const+m.const_len);
   }
   if(!variable){set.fixed_phase_code=out.all_phase_code.size();set.fixed_phase_constants=out.all_phase_consts.size();}
  }
  gpu_detail::refresh_owned_phase_patterns(out,0,out.region_phases.size());
 }
 void materialize_programs(ProgramSet& set) {
  if(set.programs_current)return;
  phase_layout_require(bool(set.host_packed),"missing owned packed executable");
  const auto& out=*set.host_packed;
  for(int p=0;p<set.count;++p)for(int k=0;k<slot_count;++k) {
   const auto& m=out.region_phases.at(set.packed_phase_indices[p*slot_count+k]).program;
   auto& program=native_phase_at(set.programs[p].bounded_region_segments[0],layout.slots[k].ordinal).program;
   program.code.clear();program.instruction_fuel.clear();program.code.reserve(m.code_len);program.instruction_fuel.reserve(m.code_len);
   for(int i=m.code_offset;i<m.code_offset+m.code_len;++i) {
    const auto x=out.all_phase_code[i];program.code.push_back({static_cast<Opcode>(x.op),x.a,x.b,bool(x.flags&1),bool(x.flags&2)});program.instruction_fuel.push_back(x.fuel);
   }
   program.consts.assign(out.all_phase_consts.begin()+m.const_offset,out.all_phase_consts.begin()+m.const_offset+m.const_len);
  }
  set.programs_current=true;
 }
 explicit Impl(std::shared_ptr<const grammar::CompiledGrammar> g):grammar(std::move(g)),
     inplace_mutation(gagp::engine_env("GAGP_GPU_INPLACE_MUTATION")!=nullptr),
     fused_donor_sites(gagp::engine_env("GAGP_GPU_FUSED_DONOR_SITES")!=nullptr),
     warp_variation(flag_is_one("GAGP_GPU_WARP_VARIATION")),
     choice_tables(warp_variation && flag_is_one("GAGP_GPU_CHOICE_TABLES")),
     compact_compile_frames(gagp::engine_env("GAGP_GPU_COMPACT_COMPILE_FRAMES")!=nullptr),
     constructive_export(gagp::engine_env("GAGP_GPU_CONSTRUCTIVE_EXPORT")!=nullptr){}
 // Only this live owner can publish construction-derived members. Cold import
 // admits the fixed skeleton and all initial phases. GPU generation samples the
 // admitted grammar/domains; same-nonterminal/scope splices preserve membership,
 // and checked physical node/depth bounds imply the admitted grammar budgets.
 // Projected profiles additionally check weighted depth on every splice; cold
 // canonical resource invariance makes construction origins valid for export.
 // export_native_phase_layout still validates node/origin/fuel/index transport.
 // A canonical reverse-parsed witness is optional cache data, not this proof.
 ProgramGenome finish_export(ProgramGenome genome) const {
  require_device();
  if(!constructive_export)
   genome.derivation=std::make_shared<const grammar::DerivationMetadata>(grammar::reconstruct_derivation(*grammar,genome));
  return genome; // no public certificate or trusted bit escapes with the AST
 }
 ~Impl(){if(start)cudaEventDestroy(start);if(stop)cudaEventDestroy(stop);}
 void require_device()const{if(!healthy)throw std::logic_error("GPU genotype owner invalidated by a failed step");int now;checked(cudaGetDevice(&now));if(now!=device)throw std::logic_error("GPU genotype device/owner mismatch");}
 std::size_t bytes()const {
  std::size_t total=(current?current->bytes():0)+(next?next->bytes():0)+(crossed?crossed->bytes():0)+(scratch?scratch->bytes():0)+slots.bytes()+fitness.bytes()+stats.bytes()+metrics.bytes()+code.bytes()+packed_code.bytes()+
      constants.bytes()+packed_constants.bytes()+compiled.bytes()+code_offsets.bytes()+constant_offsets.bytes()+parent_hashes.bytes()+parent_table.bytes()+stash_bytes()+placements.bytes();
  for(const auto& p:profiles)total+=p->bytes();return total;
 }
 void reserve_reproduction_buffers() {
  const std::size_t rows=std::size_t(population)*slot_count;
  if(!next)next=std::make_unique<Trees>(inplace_mutation?population:rows);
  if(!crossed)crossed=std::make_unique<Trees>(rows);
  if(!scratch)scratch=std::make_unique<Trees>(population);
  fitness.reserve(population);stats.reserve(population);code.reserve(rows*code_capacity);constants.reserve(rows*capacity);
  compiled.reserve(rows);code_offsets.reserve(rows+1);constant_offsets.reserve(rows+1);
  peak_transport_bytes=std::max(peak_transport_bytes,bytes());
 }
 // Steady state keeps child rows alive across evaluation. Resident owners use
 // their population-sized buffers; staged owners hold only batch-sized ones.
 void reserve_batch_buffers(int batch) {
  if(!staged_buffers){reserve_reproduction_buffers();return;}
  const std::size_t rows=std::size_t(batch)*slot_count;
  next=std::make_unique<Trees>(inplace_mutation?std::size_t(batch):rows);crossed=std::make_unique<Trees>(rows);
  scratch=std::make_unique<Trees>(batch);
  fitness.reserve(population);stats.reserve(batch);code.reserve(rows*code_capacity);constants.reserve(rows*capacity);
  compiled.reserve(rows);code_offsets.reserve(rows+1);constant_offsets.reserve(rows+1);
  peak_transport_bytes=std::max(peak_transport_bytes,bytes());
 }
 void release_reproduction_buffers() {
  peak_transport_bytes=std::max(peak_transport_bytes,bytes());
  next.reset();crossed.reset();scratch.reset();
  fitness.clear();stats.clear();code.clear();constants.clear();compiled.clear();
  code_offsets.clear();constant_offsets.clear();packed_code.clear();packed_constants.clear();
 }
 // Copies batch rows 0..b*slot_count-1 into the persistent compact stash and
 // records their node metrics, so the staged batch buffers can be released.
 void stash_batch(Trees& trees,int b) {
  const std::size_t rows=std::size_t(b)*slot_count;
  const auto lengths=trees.lengths.read(rows);
  std::vector<ImportOffset> offsets(rows);std::size_t nodes=0,values=0;
  stash_node_counts.assign(b,layout.fixed_nodes);
  for(std::size_t row=0;row<rows;++row) {
   offsets[row]={nodes,values};
   if(!valid(lengths[row]))continue;
   nodes+=lengths[row].nodes;values+=lengths[row].constants;
   stash_node_counts[row/slot_count]+=lengths[row].nodes;
  }
  metrics.reserve(std::size_t(b)*2);
  measure_site_metrics<<<(b+63)/64,64>>>(pointers(trees),b,slot_count,metrics.data);checked(cudaGetLastError());
  stash_site_metrics=metrics.read(std::size_t(b)*2);metrics.clear();
  {
   gpu_detail::CudaStageAllocationScope persistent(nullptr,false);
   stash_nodes.reserve(std::max<std::size_t>(nodes,1));stash_origins.reserve(std::max<std::size_t>(nodes,1));
   stash_sites.reserve(std::max<std::size_t>(nodes,1));stash_constants.reserve(std::max<std::size_t>(values,1));
   stash_lengths.reserve(rows);stash_offsets.upload(offsets);
  }
  stash_children<<<rows,128>>>(pointers(trees),stash_offsets.data,stash_nodes.data,stash_origins.data,stash_sites.data,
      stash_constants.data,stash_lengths.data);
  checked(cudaGetLastError());checked(cudaDeviceSynchronize());
  peak_transport_bytes=std::max(peak_transport_bytes,bytes());
 }
 // Lowers rows 0..set.count*slot_count-1 of `trees` into the program set.
 void refresh_programs(ProgramSet& set,Trees& trees,ReproductionStats* timing) {
  const int population=set.count;auto& programs=set.programs;auto& host_packed=set.host_packed;
  auto& stack_bounds=set.stack_bounds;const auto& fixed_stack_bounds=set.fixed_stack_bounds;
  const auto& packed_phase_indices=set.packed_phase_indices;const auto& initial_phase_stack_bounds=set.initial_phase_stack_bounds;
  const auto fixed_phase_code=set.fixed_phase_code,fixed_phase_constants=set.fixed_phase_constants;
  const int rows=population*slot_count;checked(cudaEventRecord(start));
  if(compact_compile_frames)compile_population<true,true><<<(rows+63)/64,64>>>(pointers(trees),slots.data,slot_count,rows,code.data,constants.data,compiled.data);
  else compile_population<false><<<(rows+63)/64,64>>>(pointers(trees),slots.data,slot_count,rows,code.data,constants.data,compiled.data);
  checked(cudaGetLastError());
  offsets_kernel<<<1,256>>>(compiled.data,rows,code_offsets.data,constant_offsets.data);checked(cudaGetLastError());
  checked(cudaEventRecord(stop));checked(cudaEventSynchronize(stop));float ms;checked(cudaEventElapsedTime(&ms,start,stop));
  if(timing)timing->kernel_ms+=ms;
  const auto begin=Clock::now();auto offsets=code_offsets.read(rows+1),values=constant_offsets.read(rows+1);
  const auto certificates=compiled.read(rows);
  if(timing)timing->copyback_ms+=elapsed(begin);
  if(offsets.back()<0 || values.back()<0)throw std::runtime_error("GPU genotype lowering rejected a constructed program");
  // The program budget covers all variable phases, not one phase buffer.
  const std::size_t variable_nodes=layout.budget.max_nodes-layout.fixed_nodes;
  phase_layout_require(std::size_t(offsets.back())<=std::size_t(population)*variable_nodes*4 &&
      std::size_t(values.back())<=std::size_t(population)*variable_nodes,"whole-program compiled capacity");
  const auto setup=Clock::now();packed_code.reserve(offsets.back());packed_constants.reserve(values.back());
  if(timing)timing->setup_ms+=elapsed(setup);checked(cudaEventRecord(start));
  compact_code<<<rows,128>>>(code.data,constants.data,code_offsets.data,constant_offsets.data,rows,packed_code.data,packed_constants.data);checked(cudaGetLastError());
  checked(cudaEventRecord(stop));checked(cudaEventSynchronize(stop));checked(cudaEventElapsedTime(&ms,start,stop));
  if(timing)timing->kernel_ms+=ms;
  const auto copy=Clock::now();const auto compacted_code=packed_code.read(offsets.back());const auto compacted_constants=packed_constants.read(values.back());
  if(timing)timing->copyback_ms+=elapsed(copy);
  const auto integrate=Clock::now();
  stack_bounds=fixed_stack_bounds;
  for(int p=0;p<population;++p)for(int k=0;k<slot_count;++k) {
   const int row=p*slot_count+k;
   if(certificates[row].status!=PhaseCompileStatus::Ok || certificates[row].stack_bound<1 || certificates[row].stack_bound>gpu_detail::MAX_STACK)
     throw std::runtime_error("GPU phase compiler certificate invalid");
   stack_bounds[p]=std::max(stack_bounds[p],certificates[row].stack_bound);
   views_proven=views_proven && certificates[row].typed_views_proven;
   private_intlists_proven=private_intlists_proven && certificates[row].private_intlists_proven;
   if(host_packed)continue;
   auto& program=native_phase_at(programs[p].bounded_region_segments[0],layout.slots[k].ordinal).program;
   program.code.clear();program.instruction_fuel.clear();program.code.reserve(offsets[row+1]-offsets[row]);program.instruction_fuel.reserve(offsets[row+1]-offsets[row]);
   for(int i=offsets[row];i<offsets[row+1];++i){const auto x=compacted_code[i];program.code.push_back({static_cast<Opcode>(x.op),x.a,x.b,bool(x.flags&1),bool(x.flags&2)});program.instruction_fuel.push_back(x.fuel);}
   program.consts.assign(compacted_constants.begin()+values[row],compacted_constants.begin()+values[row+1]);
  }
  if(host_packed) {
   auto& out=*host_packed;
   out.all_phase_code.resize(fixed_phase_code);out.all_phase_consts.resize(fixed_phase_constants);
   out.all_phase_code.insert(out.all_phase_code.end(),compacted_code.begin(),compacted_code.end());
   out.all_phase_consts.insert(out.all_phase_consts.end(),compacted_constants.begin(),compacted_constants.end());
   for(int row=0;row<rows;++row) {
    phase_layout_require(offsets[row]>=0 && offsets[row+1]>=offsets[row] && offsets[row+1]<=offsets.back() &&
        values[row]>=0 && values[row+1]>=values[row] && values[row+1]<=values.back(),"owned packed compiler offsets");
    auto& m=out.region_phases.at(packed_phase_indices[row]).program;
    m.code_offset=fixed_phase_code+offsets[row];m.code_len=offsets[row+1]-offsets[row];
    m.const_offset=fixed_phase_constants+values[row];m.const_len=values[row+1]-values[row];
   }
   // The original packer conservatively assigns the whole-program bound to all
   // phases. Preserve that capability contract, including fixed phases.
   phase_layout_require(out.region_phases.size()%population==0,"owned packed phase partition");
   const auto phases_per_program=out.region_phases.size()/population;
   // Read process settings once: this loop visits every program phase each generation.
   const bool generic_vm=std::getenv("GAGP_GENERIC_PHASE_VM")!=nullptr;
   for(int p=0;p<population;++p)for(std::size_t k=0;k<phases_per_program;++k)
    out.region_phases[p*phases_per_program+k].program.verified_stack_bound=
        generic_vm?gpu_detail::MAX_STACK:stack_bounds[p];
   if(gagp::engine_env("GAGP_PER_PHASE_STACK_BOUND") && !generic_vm) {
    // Fixed phases retain their cold verified upper bounds. Variable phases use
    // certificates from the exact freshly compiled rows copied above; the owner
    // still publishes the whole-program maximum for kernel bucket admission.
    phase_layout_require(initial_phase_stack_bounds.size()==out.region_phases.size(),"owned initial phase bound count");
    for(std::size_t i=0;i<out.region_phases.size();++i)
     out.region_phases[i].program.verified_stack_bound=initial_phase_stack_bounds[i];
    for(int row=0;row<rows;++row)
     out.region_phases.at(packed_phase_indices[row]).program.verified_stack_bound=certificates[row].stack_bound;
   }
   gpu_detail::refresh_owned_phase_patterns(out,0,out.region_phases.size());
   set.programs_current=false;
  }
  if(timing)timing->decode_ms+=elapsed(integrate); // bytecode integration only, no AST/membership decode
 }
};

NativePhasePopulation::NativePhasePopulation(std::shared_ptr<const grammar::CompiledGrammar> grammar,
    const std::vector<ProgramGenome>& input,const std::vector<std::string>& input_names,std::optional<grammar::ProjectedBudget> projected,int site_depth,
    const PhaseVariationPolicy& policy):impl_(std::make_unique<Impl>(std::move(grammar))) {
 gpu_detail::CudaStageAllocationScope construction_storage(nullptr,false);
 validate_phase_variation_policy(policy);
 impl_->operators.counted_sites=policy.site_draw==PhaseSiteDraw::Counted;
 impl_->operators.uniform_phase=policy.site_scope==PhaseSiteScope::UniformPhase;
 impl_->operators.crossover_probability=policy.crossover_probability;
 impl_->operators.crossover_attempts=policy.crossover_attempts;
 impl_->operators.mutation_max_nodes=policy.mutation_max_nodes;
 impl_->operators.deferred_copy=gagp::engine_env("GAGP_GPU_DEFERRED_CROSSOVER_COPY")!=nullptr;
 impl_->operators.fused_depth=gagp::engine_env("GAGP_GPU_FUSED_SPLICE_DEPTH")!=nullptr;
 phase_layout_require(site_depth>=0 && site_depth<=15 && !(site_depth && projected),"site depth must be0..15 and cannot combine with projected resource budgets");
 phase_layout_require(impl_->grammar && !input.empty() && input.size()<=16384,"population/grammar capacity");
 std::set<std::string> expected_names,actual_names(input_names.begin(),input_names.end());
 for(const auto& binding:impl_->grammar->inputs())expected_names.insert(binding.name);
 phase_layout_require(actual_names==expected_names && actual_names.size()==input_names.size(),"input names must exactly cover grammar inputs");
 phase_layout_require(input_names.size()<=64,"input local capacity");
 impl_->input_types.fill(ValueTag::Invalid);
 for(const auto& binding:impl_->grammar->inputs()) {
  const auto index=std::find(input_names.begin(),input_names.end(),binding.name)-input_names.begin();
  const auto type=binding.type==RType::Int?ValueTag::Int:binding.type==RType::Bool?ValueTag::Bool:
      binding.type==RType::IntList?ValueTag::IntList:ValueTag::Invalid;
  impl_->input_types[index]=type;if(type==ValueTag::Invalid)impl_->views_proven=false;
 }
 std::string error;if(!initialize_gpu_repro_runtime(&error))throw std::runtime_error(error);checked(cudaGetDevice(&impl_->device));
 std::unique_ptr<NativePhaseGrammarProfiles> import_profiles;
 if(gagp::engine_env("GAGP_REUSE_PHASE_GRAMMAR"))import_profiles=std::make_unique<NativePhaseGrammarProfiles>(impl_->grammar);
 const bool reuse_import=gagp::engine_env("GAGP_REUSE_IMPORTED_EXECUTABLE")!=nullptr;
 const auto* evaluator_inputs=reuse_import?&input_names:nullptr;
 impl_->population=input.size();impl_->layout=import_native_phase_layout(*impl_->grammar,input.front(),import_profiles.get(),evaluator_inputs,true,projected?&*projected:nullptr);
 if(site_depth)for(std::size_t k=0;k<impl_->layout.slots.size();++k)
  impl_->layout.profiles[k]=compile_phase_variation_grammar(*impl_->grammar,
      impl_->layout.slots[k].nonterminal,impl_->grammar->search_limits().max_depth,site_depth,true);
 impl_->reproduction_profile=site_depth?"native-gpu-phase-v4-site-depth"+std::to_string(site_depth):impl_->layout.slots.front().logical_depth?"native-gpu-phase-v3-projected":
     impl_->operators.counted_sites?"native-gpu-phase-v2-counted":"native-gpu-phase-v1";
 impl_->reproduction_profile+=phase_variation_profile_suffix(policy);
 if(impl_->linear_elite)impl_->reproduction_profile+="-elite-linear";
 if(impl_->parent_fitness_cache) {
  phase_layout_require(impl_->population<(1<<29),"parent fitness cache population capacity");
  impl_->parent_table_size=1;while(impl_->parent_table_size<2*impl_->population)impl_->parent_table_size*=2;
  impl_->parent_hashes.reserve(impl_->population);impl_->parent_table.reserve(impl_->parent_table_size);
  impl_->reproduction_profile+="-parent-fitness-cache";
 }
 if(impl_->operators.uniform_phase)impl_->reproduction_profile+="-uniform-phase";
 if(impl_->warp_variation)impl_->reproduction_profile+=impl_->choice_tables?"-warp-choice":"-warp";
 const auto capability=gpu_detail::pack_programs_with_shared_case_count({impl_->layout.executable},0,0);
 phase_layout_require(capability.metas.size()==1 && capability.metas.front().is_valid,"fixed executable requires GPU fallback");
 impl_->slot_count=impl_->layout.slots.size();phase_layout_require(impl_->slot_count>0 && impl_->slot_count<=8,"phase count capacity");
 const auto key=native_phase_skeleton_key(impl_->layout);const std::size_t rows=input.size()*impl_->slot_count;
 const std::size_t tree_bytes=capacity*(sizeof(PhaseTreeNode)+sizeof(int)+sizeof(PhaseGeneSite)+sizeof(Value))+sizeof(PhaseGenerationResult);
 const auto mutation_rows=impl_->inplace_mutation?input.size():rows;
 const std::size_t estimated=(2*rows+mutation_rows+input.size())*tree_bytes+rows*(code_capacity*sizeof(gpu_detail::DInstr)+capacity*sizeof(Value));
 phase_layout_require(estimated<=std::size_t(12)*1024*1024*1024,"resident genotype workspace capacity requires native fallback");
 std::size_t scratch_budget=0,free_bytes=0,total_bytes=0;
 if(gagp::engine_env("GAGP_GPU_GLOBAL_PAYLOAD"))
  phase_layout_require(gpu_detail::parse_payload_workspace_budget(gagp::engine_env("GAGP_GPU_PAYLOAD_WORKSPACE_MIB"),scratch_budget),"invalid payload workspace budget");
 checked(cudaMemGetInfo(&free_bytes,&total_bytes));
 // Kernel local-memory backing also needs device space. Retire buffers that
 // belong exclusively to the other stage before admitting the full scratch cap.
 constexpr std::size_t launch_headroom=std::size_t(2)*1024*1024*1024;
 impl_->staged_buffers=estimated>=free_bytes || scratch_budget>=free_bytes-estimated ||
     launch_headroom>=free_bytes-estimated-scratch_budget;
 if(const auto* setting=std::getenv("GAGP_GPU_PHASE_STAGED_BUFFERS")) {
  phase_layout_require(std::string(setting)=="0" || std::string(setting)=="1","staged phase buffers must be 0 or 1");
  impl_->staged_buffers=std::string(setting)=="1";
 }
 const auto* storage_flag=gagp::engine_env("GAGP_GPU_STAGE_STORAGE");
 if(storage_flag)phase_layout_require(std::string(storage_flag)=="0" || std::string(storage_flag)=="1","stage storage flag must be0 or1");
 const bool standalone_storage=storage_flag && std::string(storage_flag)=="1";
 const auto* pool_flag=gagp::engine_env("GAGP_GPU_CUDA_POOL");
 const bool requested_storage=standalone_storage || (pool_flag && std::string(pool_flag)=="1");
 if(requested_storage && impl_->staged_buffers && scratch_budget) {
  impl_->stage_storage=std::make_unique<gpu_detail::CudaStageStorage>(scratch_budget);
  impl_->reproduction_profile+="-stage-storage";
 } else if(standalone_storage)impl_->reproduction_profile+="-stage-storage-declined";
 // Persistent buffers are bounded by population × grammar capacity; no retained
 // ancestral ASTs, intern tables or generation history exist in this owner.
 impl_->current=std::make_unique<Trees>(rows);
 if(!impl_->staged_buffers){impl_->next=std::make_unique<Trees>(mutation_rows);impl_->crossed=std::make_unique<Trees>(rows);impl_->scratch=std::make_unique<Trees>(input.size());}
 std::vector<DeviceSlot> slots;
 for(int k=0;k<impl_->slot_count;++k) {
  const auto& profile=impl_->layout.profiles[k];const auto& layout=impl_->layout.slots[k];
  impl_->profiles.push_back(std::make_unique<UploadedProfile>(profile,impl_->choice_tables));
  const auto& nt=impl_->grammar->nonterminals()[layout.nonterminal];DeviceSlot slot{};
  slot.grammar=impl_->profiles.back()->view(profile);slot.root=profile.root_expression;slot.scope=nt.scope.size();slot.max_depth=layout.max_depth;slot.logical_depth=site_depth?site_depth:layout.logical_depth;
  const auto tag=[](RType t){return t==RType::Int?ValueTag::Int:t==RType::Bool?ValueTag::Bool:ValueTag::IntList;};
  slot.type=tag(nt.type);for(int j=0;j<slot.scope;++j)slot.locals[j]=tag(nt.scope[j].type);slots.push_back(slot);
 }
 impl_->slots.upload(slots);std::vector<PhaseGenerationResult> lengths(rows);
 const bool batch_import=gagp::engine_env("GAGP_BATCH_PHASE_IMPORT")!=nullptr;
 std::vector<PhaseTreeNode> import_nodes;std::vector<int> import_origins;std::vector<Value> import_constants;
 std::vector<ImportOffset> import_offsets; if(batch_import)import_offsets.reserve(rows);
 // Upload live imports only, avoiding full-capacity host staging arrays.
 for(std::size_t p=0;p<input.size();++p) {
  auto imported=p==0?impl_->layout:import_native_phase_layout(*impl_->grammar,input[p],import_profiles.get(),evaluator_inputs,!reuse_import,projected?&*projected:nullptr);
  phase_layout_require(imported.slots.size()==impl_->layout.slots.size() && native_phase_skeleton_key(imported)==key,"mixed fixed skeleton requires native fallback");
  for(int k=0;k<impl_->slot_count;++k) {
   const auto& a=imported.slots[k];const auto& b=impl_->layout.slots[k];
   phase_layout_require(a.nonterminal==b.nonterminal && a.ordinal==b.ordinal && a.binders==b.binders && a.max_depth==b.max_depth && a.logical_depth==b.logical_depth,"mixed phase contract requires fallback");
   const int row=p*impl_->slot_count+k;const auto& gene=imported.genes[k];auto& l=lengths[row];l.status=PhaseCompileStatus::Ok;l.nodes=gene.nodes.size();l.constants=gene.constants.size();
   phase_layout_require(l.nodes>0 && l.nodes<=capacity && l.constants<=l.nodes,"imported phase capacity");
   if(batch_import) {
    import_offsets.push_back({import_nodes.size(),import_constants.size()});
    import_nodes.insert(import_nodes.end(),gene.nodes.begin(),gene.nodes.end());
    import_origins.insert(import_origins.end(),gene.origins.begin(),gene.origins.end());
    import_constants.insert(import_constants.end(),gene.constants.begin(),gene.constants.end());
   } else {
   checked(cudaMemcpy(impl_->current->nodes.data+std::size_t(row)*capacity,gene.nodes.data(),gene.nodes.size()*sizeof(PhaseTreeNode),cudaMemcpyHostToDevice));
   checked(cudaMemcpy(impl_->current->origins.data+std::size_t(row)*capacity,gene.origins.data(),gene.origins.size()*sizeof(int),cudaMemcpyHostToDevice));
   if(!gene.constants.empty())checked(cudaMemcpy(impl_->current->constants.data+std::size_t(row)*capacity,gene.constants.data(),gene.constants.size()*sizeof(Value),cudaMemcpyHostToDevice));
   }
  }
  // The importer already compiled its freshly admitted immutable snapshot. With
  // the actual evaluator input order supplied, that executable needs no second
  // AST verification/lowering; GPU capacity/type checks below still apply.
  auto& programs=impl_->members.programs;
  programs.push_back(reuse_import?std::move(imported.executable):compile_for_eval(input[p],input_names));
  BytecodeVerifyOptions options;options.max_locals_per_code=gpu_detail::MAX_LOCALS;options.max_stack_depth=gpu_detail::MAX_STACK;
  const auto verified=verify_bytecode(programs.back(),options);
  phase_layout_require(bool(verified),"cold GPU executable verification failed");
  // Ordinary packing additionally checks physical region capacities. Retain
  // those inexpensive checks explicitly before creating the private carrier.
  for(const auto& segment:programs.back().bounded_region_segments) {
   phase_layout_require(segment.plan.limits.frames<=gpu_detail::DMAX_REGION_FRAMES,"cold GPU executable frame capacity exceeded");
   phase_layout_require(segment.plan.limits.cells<=gpu_detail::DMAX_REGION_MEMO,"cold GPU executable memo capacity exceeded");
  }
  impl_->members.fixed_stack_bounds.push_back(verified.verified.max_stack_depth);
  impl_->views_proven=impl_->views_proven && gpu_detail::view_program_supported(programs.back(),impl_->input_types);
  impl_->direct_root_proven=impl_->direct_root_proven && gpu_detail::direct_region_root_supported(programs.back());
  impl_->private_intlists_proven=impl_->private_intlists_proven &&
      gpu_detail::private_intlist_program_supported(programs.back(),impl_->input_types);
 }
 impl_->members.count=impl_->population;impl_->members.stack_bounds=impl_->members.fixed_stack_bounds;
 impl_->current->lengths.upload(lengths);
 if(batch_import) {
  Buffer<PhaseTreeNode> nodes;Buffer<int> origins;Buffer<Value> values;Buffer<ImportOffset> offsets;
  nodes.upload(import_nodes);origins.upload(import_origins);values.upload(import_constants);offsets.upload(import_offsets);
  impl_->peak_transport_bytes=std::max(impl_->peak_transport_bytes,impl_->bytes()+nodes.bytes()+origins.bytes()+values.bytes()+offsets.bytes());
  scatter_import<<<rows,128>>>(pointers(*impl_->current),offsets.data,nodes.data,origins.data,values.data);
  checked(cudaGetLastError());checked(cudaDeviceSynchronize());
 }
 annotate_import<<<(rows+63)/64,64>>>(pointers(*impl_->current),impl_->slots.data,rows,impl_->slot_count);checked(cudaGetLastError());
 for(const auto& r:impl_->current->lengths.read(rows))phase_layout_require(valid(r),"import site annotation failed");
 if(!impl_->staged_buffers)impl_->reserve_reproduction_buffers();
 if(gagp::engine_env("GAGP_OWNED_PHASE_PACKED")) {
  // Every program above passed cold GPU bytecode verification with the same
  // local/stack limits. No reproduction or program mutation has occurred.
  // Borrow that exact immutable vector and its bounds only for this synchronous
  // pack; later generations retain the existing checked lowering/refresh path.
  const gpu_detail::OwnedGpuPrograms initial(impl_->members.programs,impl_->members.fixed_stack_bounds);
  impl_->initialize_host_packed(impl_->members,initial);
 }
 checked(cudaEventCreate(&impl_->start));checked(cudaEventCreate(&impl_->stop));
}
NativePhasePopulation::~NativePhasePopulation() {
 if(!impl_)return;int previous=-1;const bool known=cudaGetDevice(&previous)==cudaSuccess;
 if(known && impl_->device>=0 && previous!=impl_->device)cudaSetDevice(impl_->device);
 impl_.reset();if(known && previous>=0)cudaSetDevice(previous);
}
const std::vector<BytecodeProgram>& NativePhasePopulation::programs()const{impl_->require_device();
 phase_layout_require(!impl_->steady,"steady-state members have no lowered programs");impl_->materialize_programs(impl_->members);return impl_->members.programs;}
FitnessEvalResult NativePhasePopulation::evaluate(FitnessSessionGpu& session,bool diagnostics)const {
 impl_->require_device();phase_layout_require(!impl_->steady,"steady-state members are scored by evaluate_batch");
 gpu_detail::CudaStageAllocationScope evaluation_storage(impl_->stage_storage.get(),false);
 const auto finish=[&](FitnessEvalResult result) {
  if(impl_->staged_buffers) {
   const auto begin=Clock::now();
   if(!session.release_private_workspace())throw std::runtime_error("GPU phase evaluation workspace release failed");
   const auto ms=elapsed(begin);result.timing.teardown_ms+=ms;result.timing.total_ms+=ms;
   result.payload_workspace_retained_bytes=0;
   if(impl_->stage_storage && impl_->stage_storage->live_allocations())throw std::runtime_error("evaluation left live stage scratch");
  }
  return result;
 };
 if(!gagp::engine_env("GAGP_OWNED_PHASE_PACK")) {
  impl_->fitness_reuse.clear();return finish(session.eval_programs(programs(),diagnostics));
 }
 const bool capabilities=gagp::engine_env("GAGP_OWNED_PHASE_CAPS") && impl_->views_proven;
 const gpu_detail::OwnedGpuPrograms owned(impl_->members.programs,impl_->members.stack_bounds,
     capabilities?&impl_->input_types:nullptr,capabilities && impl_->direct_root_proven,
     impl_->members.host_packed.get(),[this]{impl_->materialize_programs(impl_->members);},
     impl_->private_intlists_proven?&impl_->input_types:nullptr);
 const auto reuse_begin=Clock::now();
 const auto revision=session.evaluation_revision();
 const auto parents=impl_->fitness_reuse.mapping(revision);
 auto result=finish(session.eval_owned_programs(owned,diagnostics || impl_->fitness_reuse.enabled(),
     parents.empty()?nullptr:&parents,parents.empty()?nullptr:&impl_->fitness_reuse.completed(),revision));
 if(impl_->fitness_reuse.enabled()) {
  impl_->fitness_reuse.record(result,session.evaluation_revision());
  if(!diagnostics)result.case_counts.clear();
  result.timing.total_ms=elapsed(reuse_begin);
 }
 return result;
}
const char* NativePhasePopulation::profile_name()const{return impl_->reproduction_profile.c_str();}
std::size_t NativePhasePopulation::device_bytes()const{return std::max(impl_->bytes(),impl_->peak_transport_bytes);}
std::size_t NativePhasePopulation::stage_storage_reserved_bytes()const{return impl_->stage_storage?impl_->stage_storage->reserved_bytes():0;}
std::size_t NativePhasePopulation::stage_storage_peak_bytes()const{return impl_->stage_storage?impl_->stage_storage->peak_bytes():0;}
std::vector<std::array<unsigned,2>> NativePhasePopulation::site_metrics()const {
 impl_->require_device();impl_->metrics.reserve(impl_->population*2);
 measure_site_metrics<<<(impl_->population+63)/64,64>>>(pointers(*impl_->current),impl_->population,impl_->slot_count,impl_->metrics.data);
 checked(cudaGetLastError());const auto raw=impl_->metrics.read(impl_->population*2);
 std::vector<std::array<unsigned,2>> out;
 for(int i=0;i<impl_->population;++i) {
  if(!raw[i*2] || !raw[i*2+1])throw std::runtime_error("invalid GPU grammar-site metrics");
  out.push_back({raw[i*2],raw[i*2+1]});
 }
 return out;
}
std::vector<unsigned> NativePhasePopulation::node_counts(bool grammar_sites)const {
 if(grammar_sites) {std::vector<unsigned> counts;for(const auto& row:site_metrics())counts.push_back(row[0]);return counts;}
 impl_->require_device();const auto lengths=impl_->current->lengths.read(impl_->population*impl_->slot_count);
 std::vector<unsigned> counts(impl_->population,impl_->layout.fixed_nodes);
 for(int i=0;i<impl_->population;++i)for(int k=0;k<impl_->slot_count;++k)counts[i]+=lengths[i*impl_->slot_count+k].nodes;
 return counts;
}
ReproductionStats NativePhasePopulation::reproduce(const std::vector<double>& completed_fitness,int pressure,double probability,std::uint64_t seed,int elite_count) {
 phase_layout_require(!impl_->steady,"generational reproduction after steady state");
 phase_layout_require(elite_count>=0 && elite_count<impl_->population,"elite count");
 impl_->require_device();phase_layout_require(completed_fitness.size()==std::size_t(impl_->population) && pressure>0 && pressure<=impl_->population &&
   std::isfinite(probability) && probability>=0 && probability<=1,"selection/mutation contract");
 for(double value:completed_fitness)phase_layout_require(!std::isnan(value),"uncanonical fitness");
 gpu_detail::CudaStageAllocationScope reproduction_storage(impl_->stage_storage.get(),true);
 impl_->healthy=false;ReproductionStats timing;
 if(impl_->staged_buffers){const auto setup=Clock::now();impl_->reserve_reproduction_buffers();timing.setup_ms+=elapsed(setup);}
 const auto upload=Clock::now();impl_->fitness.upload(completed_fitness);timing.upload_ms=elapsed(upload);
 int threads=64;
 if(const char* setting=gagp::engine_env("GAGP_PHASE_REPRO_THREADS")){threads=std::atoi(setting);phase_layout_require(threads==1 || threads==2 || threads==4 || threads==8 || threads==16 || threads==32 || threads==64 || threads==128,"phase reproduction block size");}
 const int p=impl_->population,k=impl_->slot_count,available=impl_->layout.budget.max_nodes-impl_->layout.fixed_nodes;
 const auto& policy=impl_->operators;
 checked(cudaEventRecord(impl_->start));
 const auto blocks=(p+threads-1)/threads;
 if(impl_->warp_variation)crossover_population<true><<<p,32>>>(pointers(*impl_->current),pointers(*impl_->crossed),impl_->slots.data,k,p,available,
    impl_->fitness.data,pressure,seed,impl_->stats.data,policy,p);
 else crossover_population<false><<<blocks,threads>>>(pointers(*impl_->current),pointers(*impl_->crossed),impl_->slots.data,k,p,available,
    impl_->fitness.data,pressure,seed,impl_->stats.data,policy,p);
 checked(cudaGetLastError());
 const auto mutate=[&](auto kernel,int grid,int block){kernel<<<grid,block>>>(pointers(*impl_->crossed),pointers(*impl_->next),pointers(*impl_->scratch),impl_->slots.data,k,p,available,
      probability,seed,impl_->stats.data,policy);};
 const bool fused=impl_->fused_donor_sites,inplace=impl_->inplace_mutation;
 if(impl_->warp_variation) {
  if(inplace && fused)mutate(mutate_population<true,true,true>,p,32);
  else if(inplace)mutate(mutate_population<true,false,true>,p,32);
  else if(fused)mutate(mutate_population<false,true,true>,p,32);
  else mutate(mutate_population<false,false,true>,p,32);
 } else {
  if(inplace && fused)mutate(mutate_population<true,true,false>,blocks,threads);
  else if(inplace)mutate(mutate_population<true,false,false>,blocks,threads);
  else if(fused)mutate(mutate_population<false,true,false>,blocks,threads);
  else mutate(mutate_population<false,false,false>,blocks,threads);
 }
 checked(cudaGetLastError());
 auto& offspring=impl_->inplace_mutation?impl_->crossed:impl_->next;
 if(elite_count) {
  if(impl_->linear_elite && elite_count==1)preserve_one_elite<<<1,256>>>(pointers(*impl_->current),pointers(*offspring),impl_->fitness.data,p,k);
  else preserve_elites<<<(p+threads-1)/threads,threads>>>(pointers(*impl_->current),pointers(*offspring),impl_->fitness.data,p,k,elite_count);
  checked(cudaGetLastError());
 }
 compare_offspring<<<(p+threads-1)/threads,threads>>>(pointers(*impl_->current),pointers(*offspring),p,k,impl_->stats.data,p);checked(cudaGetLastError());
 if(impl_->parent_fitness_cache) {
  checked(cudaMemset(impl_->parent_table.data,0xff,impl_->parent_table.bytes()));
  index_fitness_parents<<<(p+63)/64,64>>>(pointers(*impl_->current),p,k,
      impl_->parent_hashes.data,impl_->parent_table.data,impl_->parent_table_size-1);checked(cudaGetLastError());
  find_fitness_parents<<<(p+63)/64,64>>>(pointers(*impl_->current),pointers(*offspring),p,k,
      impl_->parent_hashes.data,impl_->parent_table.data,impl_->parent_table_size-1,impl_->stats.data);checked(cudaGetLastError());
 }
 if(impl_->stage_storage){copy_current_population<<<(p*k+127)/128,128>>>(pointers(*offspring),pointers(*impl_->current),p*k);checked(cudaGetLastError());}
 checked(cudaEventRecord(impl_->stop));checked(cudaEventSynchronize(impl_->stop));float ms;checked(cudaEventElapsedTime(&ms,impl_->start,impl_->stop));timing.kernel_ms=ms;
 const auto stats=impl_->stats.read(p);
 for(const auto& one:stats) {
  if(one.error)throw std::runtime_error("GPU genotype variation error "+std::to_string(one.error));
  timing.variation.crossover_attempts+=one.crossover_attempted;timing.variation.mutation_attempts+=one.mutation;
  timing.variation.contract_rejections+=one.crossover_rejected;
  timing.variation.changed_children+=one.changed;timing.variation.unchanged_children+=!one.changed;
  timing.variation.fallback_children+=one.crossover_attempted && !one.crossover;
 }
 if(impl_->fitness_reuse.enabled()) {
  std::vector<int> parents;parents.reserve(stats.size());
  for(const auto& one:stats)parents.push_back(one.reusable_parent);
  impl_->fitness_reuse.advance(std::move(parents));
 }
 if(!impl_->stage_storage)impl_->current.swap(offspring);impl_->refresh_programs(impl_->members,*impl_->current,&timing);
 if(impl_->staged_buffers){const auto teardown=Clock::now();impl_->release_reproduction_buffers();timing.setup_ms+=elapsed(teardown);}
 if(impl_->stage_storage && impl_->stage_storage->live_allocations())throw std::runtime_error("reproduction left live stage scratch");
 impl_->healthy=true;return timing;
}
void NativePhasePopulation::begin_steady(int batch) {
 auto& s=*impl_;s.require_device();
 phase_layout_require(!s.steady && batch>=1 && batch<=s.population,"steady-state batch contract");
 // Staged owners allocate batch buffers per round (in the stage bank when
 // present) and stash the children before evaluation.
 if(!s.staged_buffers)s.reserve_batch_buffers(batch);
 if(!s.parent_table_size) {
  phase_layout_require(s.population<(1<<29),"parent fitness cache population capacity");
  s.parent_table_size=1;while(s.parent_table_size<2*s.population)s.parent_table_size*=2;
  s.parent_hashes.reserve(s.population);s.parent_table.reserve(s.parent_table_size);
 }
 // Batch programs reuse the members' fixed skeleton; variable phases are
 // replaced on every evaluate_batch. Cold bounds remain conservative.
 s.materialize_programs(s.members);
 auto& b=s.batch;b.count=batch;
 b.programs.assign(s.members.programs.begin(),s.members.programs.begin()+batch);
 b.fixed_stack_bounds.assign(s.members.fixed_stack_bounds.begin(),s.members.fixed_stack_bounds.begin()+batch);
 b.stack_bounds=b.fixed_stack_bounds;
 if(s.members.host_packed) {
  const gpu_detail::OwnedGpuPrograms initial(b.programs,b.fixed_stack_bounds);
  s.initialize_host_packed(b,initial);
 }
 s.placements.reserve(2*std::size_t(batch));
 s.steady=true;
}
std::vector<int> NativePhasePopulation::make_batch(const std::vector<double>& selection_fitness,int pressure,double probability,
    std::uint64_t seed,ReproductionStats* timing) {
 auto& s=*impl_;s.require_device();
 const int p=s.population,k=s.slot_count,b=s.batch.count,available=s.layout.budget.max_nodes-s.layout.fixed_nodes;
 phase_layout_require(s.steady && selection_fitness.size()==std::size_t(p) && pressure>0 && pressure<=p &&
   std::isfinite(probability) && probability>=0 && probability<=1,"steady-state selection/mutation contract");
 for(double value:selection_fitness)phase_layout_require(!std::isnan(value),"uncanonical fitness");
 s.healthy=false;
 gpu_detail::CudaStageAllocationScope reproduction_storage(s.stage_storage.get(),true);
 if(s.staged_buffers){const auto setup=Clock::now();s.reserve_batch_buffers(b);if(timing)timing->setup_ms+=elapsed(setup);}
 const auto upload=Clock::now();s.fitness.upload(selection_fitness);if(timing)timing->upload_ms+=elapsed(upload);
 int threads=64;
 if(const char* setting=gagp::engine_env("GAGP_PHASE_REPRO_THREADS")){threads=std::atoi(setting);phase_layout_require(threads>=1 && threads<=128 && (threads&(threads-1))==0,"phase reproduction block size");}
 const auto& policy=s.operators;const auto blocks=(b+threads-1)/threads;
 checked(cudaEventRecord(s.start));
 if(s.warp_variation)crossover_population<true><<<b,32>>>(pointers(*s.current),pointers(*s.crossed),s.slots.data,k,p,available,
    s.fitness.data,pressure,seed,s.stats.data,policy,b);
 else crossover_population<false><<<blocks,threads>>>(pointers(*s.current),pointers(*s.crossed),s.slots.data,k,p,available,
    s.fitness.data,pressure,seed,s.stats.data,policy,b);
 checked(cudaGetLastError());
 const auto mutate=[&](auto kernel,int grid,int block){kernel<<<grid,block>>>(pointers(*s.crossed),pointers(*s.next),pointers(*s.scratch),s.slots.data,k,b,available,
      probability,seed,s.stats.data,policy);};
 const bool fused=s.fused_donor_sites,inplace=s.inplace_mutation;
 if(s.warp_variation) {
  if(inplace && fused)mutate(mutate_population<true,true,true>,b,32);
  else if(inplace)mutate(mutate_population<true,false,true>,b,32);
  else if(fused)mutate(mutate_population<false,true,true>,b,32);
  else mutate(mutate_population<false,false,true>,b,32);
 } else {
  if(inplace && fused)mutate(mutate_population<true,true,false>,blocks,threads);
  else if(inplace)mutate(mutate_population<true,false,false>,blocks,threads);
  else if(fused)mutate(mutate_population<false,true,false>,blocks,threads);
  else mutate(mutate_population<false,false,false>,blocks,threads);
 }
 checked(cudaGetLastError());
 auto& offspring=s.inplace_mutation?s.crossed:s.next;
 compare_offspring<<<blocks,threads>>>(pointers(*s.current),pointers(*offspring),p,k,s.stats.data,b);checked(cudaGetLastError());
 // Any current member with an identical genotype supplies the child's result.
 checked(cudaMemset(s.parent_table.data,0xff,s.parent_table.bytes()));
 index_fitness_parents<<<(p+63)/64,64>>>(pointers(*s.current),p,k,s.parent_hashes.data,s.parent_table.data,s.parent_table_size-1);checked(cudaGetLastError());
 find_fitness_parents<<<(b+63)/64,64>>>(pointers(*s.current),pointers(*offspring),b,k,s.parent_hashes.data,s.parent_table.data,s.parent_table_size-1,s.stats.data);
 checked(cudaGetLastError());
 checked(cudaEventRecord(s.stop));checked(cudaEventSynchronize(s.stop));float ms;checked(cudaEventElapsedTime(&ms,s.start,s.stop));
 const auto stats=s.stats.read(b);std::vector<int> reuse;reuse.reserve(b);
 for(const auto& one:stats) {
  if(one.error)throw std::runtime_error("GPU genotype variation error "+std::to_string(one.error));
  phase_layout_require(one.reusable_parent>=-1 && one.reusable_parent<p,"steady-state reuse index");
  reuse.push_back(one.reusable_parent);
  if(timing) {
   timing->variation.crossover_attempts+=one.crossover_attempted;timing->variation.mutation_attempts+=one.mutation;
   timing->variation.contract_rejections+=one.crossover_rejected;
   timing->variation.changed_children+=one.changed;timing->variation.unchanged_children+=!one.changed;
   timing->variation.fallback_children+=one.crossover_attempted && !one.crossover;
  }
 }
 if(timing)timing->kernel_ms+=ms;
 s.refresh_programs(s.batch,*offspring,timing);
 if(s.staged_buffers) {
  const auto teardown=Clock::now();s.stash_batch(*offspring,b);s.release_reproduction_buffers();
  if(timing)timing->setup_ms+=elapsed(teardown);
  if(s.stage_storage && s.stage_storage->live_allocations())throw std::runtime_error("steady-state batch left live stage scratch");
 }
 s.healthy=true;return reuse;
}
FitnessEvalResult NativePhasePopulation::evaluate_batch(FitnessSessionGpu& session,const std::vector<int>& reuse,unsigned case_count,
    bool diagnostics,ReproductionStats* timing) {
 auto& s=*impl_;s.require_device();const int b=s.batch.count;
 phase_layout_require(s.steady && reuse.size()==std::size_t(b),"steady-state batch evaluation contract");
 s.healthy=false;(void)timing;
 gpu_detail::CudaStageAllocationScope evaluation_storage(s.stage_storage.get(),false);
 const bool capabilities=gagp::engine_env("GAGP_OWNED_PHASE_CAPS") && s.views_proven;
 const gpu_detail::OwnedGpuPrograms owned(s.batch.programs,s.batch.stack_bounds,
     capabilities?&s.input_types:nullptr,capabilities && s.direct_root_proven,
     s.batch.host_packed.get(),[&s]{s.materialize_programs(s.batch);},
     s.private_intlists_proven?&s.input_types:nullptr);
 // Reused children are skipped by the evaluator; their placeholder results are
 // replaced by the caller with the identical member's completed result.
 std::vector<int> mapping(b,-1);FitnessEvalResult placeholder;bool any=false;
 for(int i=0;i<b;++i)if(reuse[i]>=0){mapping[i]=i;any=true;}
 if(any) {
  placeholder.ok=true;placeholder.fitness.assign(b,0.0);placeholder.case_counts.assign(b,{case_count,0,0,0,0});
  placeholder.wrong.assign(b,0);
 }
 auto result=session.eval_owned_programs(owned,true,any?&mapping:nullptr,any?&placeholder:nullptr,session.evaluation_revision());
 if(s.staged_buffers && !session.release_private_workspace())throw std::runtime_error("GPU phase evaluation workspace release failed");
 if(s.stage_storage && s.stage_storage->live_allocations())throw std::runtime_error("steady-state evaluation left live stage scratch");
 if(!diagnostics)result.case_counts.clear();
 s.healthy=true;return result;
}
std::vector<unsigned> NativePhasePopulation::batch_nodes(bool grammar_sites)const {
 const auto& s=*impl_;s.require_device();const int b=s.batch.count;phase_layout_require(s.steady,"steady-state batch nodes");
 if(s.staged_buffers) {
  if(!grammar_sites)return s.stash_node_counts;
  std::vector<unsigned> out;out.reserve(b);
  for(int i=0;i<b;++i){if(!s.stash_site_metrics[i*2] || !s.stash_site_metrics[i*2+1])throw std::runtime_error("invalid GPU grammar-site metrics");out.push_back(s.stash_site_metrics[i*2]);}
  return out;
 }
 auto& offspring=s.inplace_mutation?*s.crossed:*s.next;
 if(grammar_sites) {
  impl_->metrics.reserve(b*2);
  measure_site_metrics<<<(b+63)/64,64>>>(pointers(offspring),b,s.slot_count,impl_->metrics.data);checked(cudaGetLastError());
  const auto raw=impl_->metrics.read(b*2);std::vector<unsigned> out;
  for(int i=0;i<b;++i){if(!raw[i*2] || !raw[i*2+1])throw std::runtime_error("invalid GPU grammar-site metrics");out.push_back(raw[i*2]);}
  return out;
 }
 const auto lengths=offspring.lengths.read(std::size_t(b)*s.slot_count);
 std::vector<unsigned> counts(b,s.layout.fixed_nodes);
 for(int i=0;i<b;++i)for(int k=0;k<s.slot_count;++k)counts[i]+=lengths[i*s.slot_count+k].nodes;
 return counts;
}
void NativePhasePopulation::commit_batch(const std::vector<std::array<int,2>>& placements) {
 auto& s=*impl_;s.require_device();phase_layout_require(s.steady && placements.size()<=std::size_t(s.batch.count),"steady-state placements");
 if(placements.empty())return;
 std::vector<int> flat;std::vector<bool> member_seen(s.population,false);
 for(const auto& [member,child]:placements) {
  phase_layout_require(member>=0 && member<s.population && child>=0 && child<s.batch.count && !member_seen[member],"steady-state placement");
  member_seen[member]=true;flat.push_back(member);flat.push_back(child);
 }
 s.healthy=false;s.placements.upload(flat);
 const int items=int(placements.size())*s.slot_count;
 if(s.staged_buffers)place_stashed<<<items,128>>>(pointers(*s.current),s.placements.data,s.slot_count,s.stash_offsets.data,s.stash_nodes.data,
     s.stash_origins.data,s.stash_sites.data,s.stash_constants.data,s.stash_lengths.data);
 else place_children<<<(items+127)/128,128>>>(pointers(*(s.inplace_mutation?s.crossed:s.next)),pointers(*s.current),s.placements.data,int(placements.size()),s.slot_count);
 checked(cudaGetLastError());checked(cudaDeviceSynchronize());
 s.healthy=true;
}

std::vector<ProgramGenome> NativePhasePopulation::export_population()const {
 impl_->require_device();
 const std::size_t rows=std::size_t(impl_->population)*impl_->slot_count;
 const auto lengths=impl_->current->lengths.read(rows);std::vector<ImportOffset> host_offsets;
 host_offsets.reserve(rows);std::size_t node_count=0,constant_count=0;
 for(const auto& r:lengths) {
  phase_layout_require(valid(r),"invalid bulk export lengths");
  host_offsets.push_back({node_count,constant_count});node_count+=r.nodes;constant_count+=r.constants;
 }
 // Capacity/population are checked on owner construction. Packed live counts
 // cannot exceed those allocations; temporaries do not outlive this call.
 Buffer<ImportOffset> offsets;offsets.upload(host_offsets);
 Buffer<PhaseTreeNode> nodes;Buffer<int> origins;Buffer<Value> values;
 nodes.reserve(node_count);origins.reserve(node_count);values.reserve(constant_count);
 impl_->peak_transport_bytes=std::max(impl_->peak_transport_bytes,impl_->bytes()+offsets.bytes()+nodes.bytes()+origins.bytes()+values.bytes());
 gather_export<<<rows,128>>>(pointers(*impl_->current),offsets.data,nodes.data,origins.data,values.data);
 checked(cudaGetLastError());
 const auto host_nodes=nodes.read(node_count);const auto host_origins=origins.read(node_count);const auto host_values=values.read(constant_count);
 std::vector<ProgramGenome> result;result.reserve(impl_->population);
 for(int p=0;p<impl_->population;++p) {
  std::vector<HostPhaseGene> genes(impl_->slot_count);
  for(int k=0;k<impl_->slot_count;++k) {
   const std::size_t row=std::size_t(p)*impl_->slot_count+k;const auto r=lengths[row];const auto offset=host_offsets[row];auto& gene=genes[k];
   gene.nodes.assign(host_nodes.begin()+offset.nodes,host_nodes.begin()+offset.nodes+r.nodes);
   gene.origins.assign(host_origins.begin()+offset.nodes,host_origins.begin()+offset.nodes+r.nodes);
   gene.constants.assign(host_values.begin()+offset.constants,host_values.begin()+offset.constants+r.constants);
  }
  result.push_back(impl_->finish_export(export_native_phase_layout(*impl_->grammar,impl_->layout,genes)));
 }
 return result;
}
ProgramGenome NativePhasePopulation::export_member(std::size_t index)const {
 impl_->require_device();phase_layout_require(index<std::size_t(impl_->population),"export index");
 std::vector<HostPhaseGene> genes(impl_->slot_count);
 for(int k=0;k<impl_->slot_count;++k) {
  const std::size_t row=index*impl_->slot_count+k;PhaseGenerationResult r;
  checked(cudaMemcpy(&r,impl_->current->lengths.data+row,sizeof(r),cudaMemcpyDeviceToHost));phase_layout_require(valid(r),"invalid export lengths");
  auto& gene=genes[k];gene.nodes.resize(r.nodes);gene.origins.resize(r.nodes);gene.constants.resize(r.constants);
  checked(cudaMemcpy(gene.nodes.data(),impl_->current->nodes.data+row*capacity,r.nodes*sizeof(PhaseTreeNode),cudaMemcpyDeviceToHost));
  checked(cudaMemcpy(gene.origins.data(),impl_->current->origins.data+row*capacity,r.nodes*sizeof(int),cudaMemcpyDeviceToHost));
  if(r.constants)checked(cudaMemcpy(gene.constants.data(),impl_->current->constants.data+row*capacity,r.constants*sizeof(Value),cudaMemcpyDeviceToHost));
 }
 return impl_->finish_export(export_native_phase_layout(*impl_->grammar,impl_->layout,genes));
}
} // namespace gagp::evo::repro
