#include "../../../runtime/gpu/owned_programs.hpp"
#include "../../../runtime/gpu/view_profile.hpp"
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "gagp/core/bytecode_verify.hpp"
#include "phase_population.hpp"
#include "phase_layout.hpp"
#include "internal.hpp"
#include "gagp/runtime/gpu/host_pack_gpu.hpp"
#include <cuda_runtime.h>
#include <chrono>
#include <cmath>
#include <limits>

namespace gagp::evo::repro {
namespace {
constexpr int capacity = 1024, code_capacity = 4*capacity;
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); }
void checked(cudaError_t status) { if (status != cudaSuccess) throw std::runtime_error(cudaGetErrorString(status)); }
template<class T> struct Buffer {
 T* data=nullptr;std::size_t count=0;
 Buffer()=default;Buffer(const Buffer&)=delete;
 ~Buffer(){if(data)cudaFree(data);}
 void reserve(std::size_t size) {
  if(size<=count)return;
  if(size>std::numeric_limits<std::size_t>::max()/sizeof(T))throw std::overflow_error("GPU genotype allocation overflow");
  T* next=nullptr;checked(cudaMalloc(reinterpret_cast<void**>(&next),size*sizeof(T)));
  if(data)checked(cudaFree(data));data=next;count=size;
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
 explicit UploadedProfile(const HostPhaseGrammar& h) {
  expressions.upload(h.expressions);
  if(std::getenv("GAGP_GPU_STATIC_WEIGHTS")) {
   auto weights=h.productions;auto nts=h.nonterminals;normalize_static_phase_weights(h,nts,weights);
   productions.upload(weights);nonterminals.upload(nts);
  } else {productions.upload(h.productions);nonterminals.upload(h.nonterminals);}
  domains.upload(h.domains);values.upload(h.values);minimum.upload(h.minimum);
 }
 PhaseGrammarView view(const HostPhaseGrammar& h)const{return {expressions.data,int(h.expressions.size()),productions.data,int(h.productions.size()),
  nonterminals.data,int(h.nonterminals.size()),domains.data,int(h.domains.size()),values.data,int(h.values.size()),minimum.data,h.depth_count};}
 std::size_t bytes()const{return expressions.bytes()+productions.bytes()+nonterminals.bytes()+domains.bytes()+values.bytes()+minimum.bytes();}
};
struct DeviceSlot {PhaseGrammarView grammar;int root,scope,max_depth;ValueTag type;ValueTag locals[8];};
struct Trees {
 Buffer<PhaseTreeNode> nodes;Buffer<int> origins;Buffer<PhaseGeneSite> sites;Buffer<Value> constants;Buffer<PhaseGenerationResult> lengths;
 explicit Trees(std::size_t count){nodes.reserve(count*capacity);origins.reserve(count*capacity);sites.reserve(count*capacity);constants.reserve(count*capacity);lengths.reserve(count);}
 std::size_t bytes()const{return nodes.bytes()+origins.bytes()+sites.bytes()+constants.bytes()+lengths.bytes();}
};
struct TreePointers {
 PhaseTreeNode* nodes;int* origins;PhaseGeneSite* sites;Value* constants;PhaseGenerationResult* lengths;
 __device__ PhaseTreeView view(int row)const {return {nodes+std::size_t(row)*capacity,origins+std::size_t(row)*capacity,sites+std::size_t(row)*capacity,
   constants+std::size_t(row)*capacity,lengths[row].nodes,lengths[row].constants};}
 __device__ MutablePhaseTree output(int row)const{return {nodes+std::size_t(row)*capacity,origins+std::size_t(row)*capacity,
   sites+std::size_t(row)*capacity,constants+std::size_t(row)*capacity,capacity};}
};
TreePointers pointers(Trees& t){return {t.nodes.data,t.origins.data,t.sites.data,t.constants.data,t.lengths.data};}
__host__ __device__ bool valid(const PhaseGenerationResult& r){return r.status==PhaseCompileStatus::Ok && r.nodes>0 && r.nodes<=capacity && r.constants>=0 && r.constants<=r.nodes;}
__device__ void copy_tree(TreePointers from,int source,TreePointers to,int target) {
 const auto r=from.lengths[source];to.lengths[target]={};if(!valid(r))return;
 const auto a=from.view(source);const auto b=to.output(target);
 for(int i=0;i<r.nodes;++i){b.nodes[i]=a.nodes[i];b.origins[i]=a.origins[i];b.sites[i]=a.sites[i];}
 for(int i=0;i<r.constants;++i)b.constants[i]=a.constants[i];
 to.lengths[target]=r;
}
__global__ void annotate_import(TreePointers trees,const DeviceSlot* slots,int rows,int slot_count) {
 const int row=blockIdx.x*blockDim.x+threadIdx.x;if(row>=rows)return;const auto& s=slots[row%slot_count];const auto t=trees.view(row);
 if(!valid(trees.lengths[row]) || !annotate_phase_tree(s.grammar,s.root,s.scope,t.nodes,t.origins,t.node_count,trees.output(row).sites))trees.lengths[row].status=PhaseCompileStatus::Invalid;
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
__device__ int tournament(const double* fitness,int population,int pressure,DGrammarRandom& random) {
 int best=random.bounded(population);
 for(int i=1;i<pressure;++i){const int candidate=random.bounded(population);if(fitness[candidate]>fitness[best])best=candidate;}
 return best;
}
struct Site {int row=-1,node=-1;};
__device__ Site choose_site(TreePointers trees,int program,int slots,DGrammarRandom& random,bool counted,int nt=-1,int scope=-1) {
 if(counted) {
  unsigned count=0;
  for(int k=0;k<slots;++k) {
   const int row=program*slots+k;if(!valid(trees.lengths[row]))return {};
   const auto tree=trees.view(row);
   for(int i=0;i<tree.node_count;++i)if(tree.sites[i].nonterminal>=0 &&
       (nt<0 || (tree.sites[i].nonterminal==nt && tree.sites[i].scope==scope)))++count;
  }
  if(!count)return {};
  unsigned rank=random.bounded(count);
  for(int k=0;k<slots;++k) {
   const int row=program*slots+k;const auto tree=trees.view(row);
   for(int i=0;i<tree.node_count;++i)if(tree.sites[i].nonterminal>=0 &&
       (nt<0 || (tree.sites[i].nonterminal==nt && tree.sites[i].scope==scope))) {
     if(rank==0)return {row,i};--rank;
   }
  }
  return {};
 }
 Site selected;unsigned count=0;
 for(int k=0;k<slots;++k) {
  const int row=program*slots+k;if(!valid(trees.lengths[row]))return {};
  const auto tree=trees.view(row);
  for(int i=0;i<tree.node_count;++i)if(tree.sites[i].nonterminal>=0 &&
      (nt<0 || (tree.sites[i].nonterminal==nt && tree.sites[i].scope==scope)))
    if(random.bounded(++count)==0)selected={row,i};
 }
 return selected;
}
struct OperatorStats {int parent=-1,crossover=0,crossover_rejected=0,mutation=0,changed=0,error=0;};
__global__ void crossover_population(TreePointers input,TreePointers output,const DeviceSlot* slots,
    int slot_count,int population,int max_variable_nodes,const double* fitness,int pressure,std::uint64_t seed,OperatorStats* stats,bool counted) {
 const int child=blockIdx.x*blockDim.x+threadIdx.x;if(child>=population)return;
 DGrammarRandom random(seed ^ (UINT64_C(0x617089e168) + child));auto& stat=stats[child];stat={};
 const int parent=tournament(fitness,population,pressure,random),donor=tournament(fitness,population,pressure,random);stat.parent=parent;
 int total=0;
 for(int k=0;k<slot_count;++k) {
  const int row=parent*slot_count+k;
  if(!valid(input.lengths[row])){stat.error=1;return;}
  total+=input.lengths[row].nodes;copy_tree(input,row,output,child*slot_count+k);
 }
 // Bounded retry changes site sampling from the legacy compatible-pair policy.
 // Each rejection is explicit; fitness candidates and full evaluation stay intact.
 for(int attempt=0;attempt<8;++attempt) {
  const auto destination=choose_site(input,parent,slot_count,random,counted);if(destination.node<0){stat.error=2;return;}
  const auto a=input.view(destination.row);const auto contract=a.sites[destination.node];
  const auto source=choose_site(input,donor,slot_count,random,counted,contract.nonterminal,contract.scope);
  if(source.node<0){++stat.crossover_rejected;continue;}
  const auto b=input.view(source.row);const int ae=phase_subtree_end(a,destination.node),be=phase_subtree_end(b,source.node);
  if(ae<0 || be<0){stat.error=3;return;}
  if(total-(ae-destination.node)+(be-source.node)>max_variable_nodes){++stat.crossover_rejected;continue;}
  const int slot=destination.row%slot_count,target=child*slot_count+slot;
  auto r=splice_phase_tree(a,destination.node,b,source.node,output.output(target),slots[slot].max_depth);
  if(r.status==PhaseCompileStatus::Ok){output.lengths[target]=r;stat.crossover=1;return;}
  // Failed bounded speculation can have partially written the output buffer.
  // Restore the complete parent before any subsequent attempt or publication.
  copy_tree(input,destination.row,output,target);++stat.crossover_rejected;
 }
}
template<bool InPlace, bool Fused>
__global__ void mutate_population(TreePointers input,TreePointers output,TreePointers scratch,const DeviceSlot* slots,
    int slot_count,int population,int max_variable_nodes,double probability,std::uint64_t seed,OperatorStats* stats,bool counted) {
 const int child=blockIdx.x*blockDim.x+threadIdx.x;if(child>=population)return;auto& stat=stats[child];if(stat.error)return;
 int total=0;
 for(int k=0;k<slot_count;++k){const int row=child*slot_count+k;if(!valid(input.lengths[row])){stat.error=4;return;}total+=input.lengths[row].nodes;if constexpr(!InPlace)copy_tree(input,row,output,row);}
 DGrammarRandom random(seed ^ (UINT64_C(0x124ab512bf01)+child));
 if(static_cast<double>(random.next()>>11)*0x1.0p-53>=probability)return;
 const auto destination=choose_site(input,child,slot_count,random,counted);if(destination.node<0){stat.error=5;return;}
 const auto a=input.view(destination.row);const int end=phase_subtree_end(a,destination.node),depth=phase_node_depth(a,destination.node);
 if(end<0 || depth<0){stat.error=6;return;}
 const auto site=a.sites[destination.node];const auto& slot=slots[destination.row%slot_count];auto tmp=scratch.output(child);
 const int available=max_variable_nodes-(total-(end-destination.node));
 auto generated=Fused
   ? generate_phase<true,true>(slot.grammar,site.entry_expression,site.parent_scope,slot.max_depth-depth+1,
       min(capacity,available),random.next(),tmp.nodes,tmp.origins,tmp.constants,tmp.sites)
   : generate_phase(slot.grammar,site.entry_expression,site.parent_scope,slot.max_depth-depth+1,
       min(capacity,available),random.next(),tmp.nodes,tmp.origins,tmp.constants);
 if(generated.status!=PhaseCompileStatus::Ok || (!Fused && !annotate_phase_tree(slot.grammar,site.entry_expression,site.parent_scope,
       tmp.nodes,tmp.origins,generated.nodes,tmp.sites))){stat.error=7;return;}
 scratch.lengths[child]=generated;
 // Splicing may partially write on failure, so its destination must never
 // alias the crossed input. Each child owns a separate temporary row.
 const int target=InPlace?child:destination.row;
 auto r=splice_phase_tree(a,destination.node,scratch.view(child),0,output.output(target),slot.max_depth);
 if(r.status!=PhaseCompileStatus::Ok){stat.error=8;return;}
 output.lengths[target]=r;
 if constexpr(InPlace)copy_tree(output,target,input,destination.row);
 stat.mutation=1;
}
__global__ void compare_offspring(TreePointers parents,TreePointers children,int population,int slot_count,OperatorStats* stats) {
 const int child=blockIdx.x*blockDim.x+threadIdx.x;if(child>=population || stats[child].error)return;
 const int parent=stats[child].parent;if(parent<0 || parent>=population){stats[child].error=9;return;}
 bool equal=true;
 for(int k=0;k<slot_count && equal;++k) {
  const auto a=parents.view(parent*slot_count+k),b=children.view(child*slot_count+k);
  if(a.node_count!=b.node_count || a.constant_count!=b.constant_count){equal=false;break;}
  for(int i=0;i<a.node_count && equal;++i) {
   const auto x=a.nodes[i],y=b.nodes[i];equal=x.kind==y.kind && x.opcode==y.opcode && x.operand==y.operand &&
      x.arity==y.arity && x.fuel==y.fuel && x.merge_fuel==y.merge_fuel && a.origins[i]==b.origins[i];
  }
  for(int i=0;i<a.constant_count && equal;++i){const auto x=a.constants[i],y=b.constants[i];equal=x.tag==y.tag && (x.tag==ValueTag::Bool?x.b==y.b:x.i==y.i);}
 }
 stats[child].changed=!equal;
}
__global__ void compile_population(TreePointers trees,const DeviceSlot* slots,int slot_count,int rows,
    gpu_detail::DInstr* code,Value* constants,PhaseCompileResult* compiled) {
 const int row=blockIdx.x*blockDim.x+threadIdx.x;if(row>=rows)return;
 const auto t=trees.view(row);const auto& slot=slots[row%slot_count];
 if(!valid(trees.lengths[row])){compiled[row]={};return;}
 compiled[row]=compile_phase_tree(t.nodes,t.node_count,t.constants,t.constant_count,slot.locals,slot.scope,slot.type,
     code+std::size_t(row)*code_capacity,code_capacity,constants+std::size_t(row)*capacity,capacity);
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
RegionPhase& phase_at(BoundedRegionSegment& segment,std::size_t ordinal) {
 switch(bounded_region_phase_kind(segment.plan,ordinal)) {
  case RegionPhaseKind::BasePredicate:return segment.base_predicate;
  case RegionPhaseKind::BaseBody:return segment.base_body;
  case RegionPhaseKind::Combine:return segment.combine;
  case RegionPhaseKind::Boundary:return *segment.boundary;
  case RegionPhaseKind::Preparation:return segment.preparations.at(bounded_region_preparation_ordinal(segment.plan,ordinal));
  case RegionPhaseKind::Request:return segment.request_expressions.at(ordinal-2-segment.preparations.size());
 }
 throw std::logic_error("GPU genotype phase ordinal");
}
// A cold structural key only. Neutral expression placeholders are never executed
// or offered as grammar members; their purpose is to compare the fixed skeleton.
std::string skeleton_key(const NativePhaseLayout& layout) {
 ProgramGenome skeleton;skeleton.ast=layout.prototype;auto& ast=skeleton.ast;ast.nodes.clear();ast.fuel_specs.clear();
 const int marker=ast.consts.size();ast.consts.push_back(Value::from_int(0));
 std::vector<std::size_t> remap(layout.prototype.nodes.size(),std::size_t(-1));std::size_t slot=0;
 for(std::size_t i=0;i<layout.prototype.nodes.size();) {
  if(slot<layout.slots.size() && layout.slots[slot].begin==i){ast.nodes.push_back({NodeKind::CONST,marker});i=layout.slots[slot++].end;}
  else {remap[i]=ast.nodes.size();ast.nodes.push_back(layout.prototype.nodes[i]);++i;}
 }
 for(const auto& fuel:layout.prototype.fuel_specs)if(remap.at(fuel.node_index)!=std::size_t(-1))ast.fuel_specs.push_back({remap[fuel.node_index],fuel.charges});
 for(auto& region:ast.bounded_region_specs)region.node_index=remap.at(region.node_index);
 return ast_cache_key(compact_genome_tables(std::move(skeleton)).ast);
}
} // namespace

struct NativePhasePopulation::Impl {
 std::shared_ptr<const grammar::CompiledGrammar> grammar;
 const bool counted_sites;
 const bool inplace_mutation;
 const bool constructive_export;
 std::size_t peak_transport_bytes=0;
 NativePhaseLayout layout;int population=0,slot_count=0,device=-1;bool healthy=true;
 std::vector<std::unique_ptr<UploadedProfile>> profiles;Buffer<DeviceSlot> slots;
 std::unique_ptr<Trees> current,next,crossed,scratch;
 Buffer<double> fitness;Buffer<OperatorStats> stats;
 Buffer<gpu_detail::DInstr> code,packed_code;Buffer<Value> constants,packed_constants;
 Buffer<PhaseCompileResult> compiled;Buffer<int> code_offsets,constant_offsets;
 std::vector<int> fixed_stack_bounds,stack_bounds;
 std::array<ValueTag,64> input_types;bool views_proven=true,direct_root_proven=true;
 std::vector<BytecodeProgram> programs;cudaEvent_t start=nullptr,stop=nullptr;
 std::unique_ptr<gpu_detail::PackResult> host_packed;
 std::vector<int> packed_phase_indices;
 std::size_t fixed_phase_code=0,fixed_phase_constants=0;
 bool programs_current=true;
 void initialize_host_packed() {
  host_packed=std::make_unique<gpu_detail::PackResult>(gpu_detail::pack_programs_with_shared_case_count(programs,0,0));
  auto& out=*host_packed;std::vector<bool> dynamic(out.region_phases.size(),false);
  for(int p=0;p<population;++p) {
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
   if(!variable){fixed_phase_code=out.all_phase_code.size();fixed_phase_constants=out.all_phase_consts.size();}
  }
  gpu_detail::refresh_owned_phase_patterns(out,0,out.region_phases.size());
 }
 void materialize_programs() {
  if(programs_current)return;
  phase_layout_require(bool(host_packed),"missing owned packed executable");
  const auto& out=*host_packed;
  for(int p=0;p<population;++p)for(int k=0;k<slot_count;++k) {
   const auto& m=out.region_phases.at(packed_phase_indices[p*slot_count+k]).program;
   auto& program=phase_at(programs[p].bounded_region_segments[0],layout.slots[k].ordinal).program;
   program.code.clear();program.instruction_fuel.clear();program.code.reserve(m.code_len);program.instruction_fuel.reserve(m.code_len);
   for(int i=m.code_offset;i<m.code_offset+m.code_len;++i) {
    const auto x=out.all_phase_code[i];program.code.push_back({static_cast<Opcode>(x.op),x.a,x.b,bool(x.flags&1),bool(x.flags&2)});program.instruction_fuel.push_back(x.fuel);
   }
   program.consts.assign(out.all_phase_consts.begin()+m.const_offset,out.all_phase_consts.begin()+m.const_offset+m.const_len);
  }
  programs_current=true;
 }
 explicit Impl(std::shared_ptr<const grammar::CompiledGrammar> g):grammar(std::move(g)),counted_sites(std::getenv("GAGP_COUNTED_SITE_DRAW")!=nullptr),
     inplace_mutation(std::getenv("GAGP_GPU_INPLACE_MUTATION")!=nullptr),
     constructive_export(std::getenv("GAGP_GPU_CONSTRUCTIVE_EXPORT")!=nullptr){}
 // Only this live owner can publish construction-derived members. Cold import
 // admits the fixed skeleton and all initial phases. GPU generation samples the
 // admitted grammar/domains; same-nonterminal/scope splices preserve membership,
 // and checked physical node/depth bounds imply the admitted grammar budgets.
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
  std::size_t total=current->bytes()+next->bytes()+crossed->bytes()+scratch->bytes()+slots.bytes()+fitness.bytes()+stats.bytes()+code.bytes()+packed_code.bytes()+
      constants.bytes()+packed_constants.bytes()+compiled.bytes()+code_offsets.bytes()+constant_offsets.bytes();
  for(const auto& p:profiles)total+=p->bytes();return total;
 }
 void refresh_programs(ReproductionStats* timing) {
  const int rows=population*slot_count;checked(cudaEventRecord(start));
  compile_population<<<(rows+63)/64,64>>>(pointers(*current),slots.data,slot_count,rows,code.data,constants.data,compiled.data);checked(cudaGetLastError());
  offsets_kernel<<<1,256>>>(compiled.data,rows,code_offsets.data,constant_offsets.data);checked(cudaGetLastError());
  checked(cudaEventRecord(stop));checked(cudaEventSynchronize(stop));float ms;checked(cudaEventElapsedTime(&ms,start,stop));
  if(timing)timing->kernel_ms+=ms;
  const auto begin=Clock::now();auto offsets=code_offsets.read(rows+1),values=constant_offsets.read(rows+1);
  const auto certificates=compiled.read(rows);
  if(timing)timing->copyback_ms+=elapsed(begin);
  if(offsets.back()<0 || values.back()<0)throw std::runtime_error("GPU genotype lowering rejected a constructed program");
  phase_layout_require(offsets.back()<=population*capacity*4 && values.back()<=population*capacity,"whole-program compiled capacity");
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
   if(host_packed)continue;
   auto& program=phase_at(programs[p].bounded_region_segments[0],layout.slots[k].ordinal).program;
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
   for(int p=0;p<population;++p)for(std::size_t k=0;k<phases_per_program;++k)
    out.region_phases[p*phases_per_program+k].program.verified_stack_bound=
        std::getenv("GAGP_GENERIC_PHASE_VM")?gpu_detail::MAX_STACK:stack_bounds[p];
   gpu_detail::refresh_owned_phase_patterns(out,0,out.region_phases.size());
   programs_current=false;
  }
  if(timing)timing->decode_ms+=elapsed(integrate); // bytecode integration only, no AST/membership decode
 }
};

NativePhasePopulation::NativePhasePopulation(std::shared_ptr<const grammar::CompiledGrammar> grammar,
    const std::vector<ProgramGenome>& input,const std::vector<std::string>& input_names):impl_(std::make_unique<Impl>(std::move(grammar))) {
 phase_layout_require(impl_->grammar && !input.empty() && input.size()<=8192,"population/grammar capacity");
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
 if(std::getenv("GAGP_REUSE_PHASE_GRAMMAR"))import_profiles=std::make_unique<NativePhaseGrammarProfiles>(impl_->grammar);
 const bool reuse_import=std::getenv("GAGP_REUSE_IMPORTED_EXECUTABLE")!=nullptr;
 const auto* evaluator_inputs=reuse_import?&input_names:nullptr;
 impl_->population=input.size();impl_->layout=import_native_phase_layout(*impl_->grammar,input.front(),import_profiles.get(),evaluator_inputs);
 const auto capability=gpu_detail::pack_programs_with_shared_case_count({impl_->layout.executable},0,0);
 phase_layout_require(capability.metas.size()==1 && capability.metas.front().is_valid,"fixed executable requires GPU fallback");
 impl_->slot_count=impl_->layout.slots.size();phase_layout_require(impl_->slot_count>0 && impl_->slot_count<=8,"phase count capacity");
 const auto key=skeleton_key(impl_->layout);const std::size_t rows=input.size()*impl_->slot_count;
 const std::size_t tree_bytes=capacity*(sizeof(PhaseTreeNode)+sizeof(int)+sizeof(PhaseGeneSite)+sizeof(Value))+sizeof(PhaseGenerationResult);
 const auto mutation_rows=impl_->inplace_mutation?input.size():rows;
 const std::size_t estimated=(2*rows+mutation_rows+input.size())*tree_bytes+rows*(code_capacity*sizeof(gpu_detail::DInstr)+capacity*sizeof(Value));
 phase_layout_require(estimated<=std::size_t(12)*1024*1024*1024,"resident genotype workspace capacity requires native fallback");
 // Persistent buffers are bounded by population × grammar capacity; no retained
 // ancestral ASTs, intern tables or generation history exist in this owner.
 impl_->current=std::make_unique<Trees>(rows);impl_->next=std::make_unique<Trees>(mutation_rows);impl_->crossed=std::make_unique<Trees>(rows);impl_->scratch=std::make_unique<Trees>(input.size());
 std::vector<DeviceSlot> slots;
 for(int k=0;k<impl_->slot_count;++k) {
  const auto& profile=impl_->layout.profiles[k];const auto& layout=impl_->layout.slots[k];
  impl_->profiles.push_back(std::make_unique<UploadedProfile>(profile));
  const auto& nt=impl_->grammar->nonterminals()[layout.nonterminal];DeviceSlot slot{};
  slot.grammar=impl_->profiles.back()->view(profile);slot.root=profile.root_expression;slot.scope=nt.scope.size();slot.max_depth=layout.max_depth;
  const auto tag=[](RType t){return t==RType::Int?ValueTag::Int:t==RType::Bool?ValueTag::Bool:ValueTag::IntList;};
  slot.type=tag(nt.type);for(int j=0;j<slot.scope;++j)slot.locals[j]=tag(nt.scope[j].type);slots.push_back(slot);
 }
 impl_->slots.upload(slots);std::vector<PhaseGenerationResult> lengths(rows);
 const bool batch_import=std::getenv("GAGP_BATCH_PHASE_IMPORT")!=nullptr;
 std::vector<PhaseTreeNode> import_nodes;std::vector<int> import_origins;std::vector<Value> import_constants;
 std::vector<ImportOffset> import_offsets; if(batch_import)import_offsets.reserve(rows);
 // Upload live imports only, avoiding full-capacity host staging arrays.
 for(std::size_t p=0;p<input.size();++p) {
  auto imported=p==0?impl_->layout:import_native_phase_layout(*impl_->grammar,input[p],import_profiles.get(),evaluator_inputs,!reuse_import);
  phase_layout_require(imported.slots.size()==impl_->layout.slots.size() && skeleton_key(imported)==key,"mixed fixed skeleton requires native fallback");
  for(int k=0;k<impl_->slot_count;++k) {
   const auto& a=imported.slots[k];const auto& b=impl_->layout.slots[k];
   phase_layout_require(a.nonterminal==b.nonterminal && a.ordinal==b.ordinal && a.binders==b.binders && a.max_depth==b.max_depth,"mixed phase contract requires fallback");
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
  impl_->programs.push_back(reuse_import?std::move(imported.executable):compile_for_eval(input[p],input_names));
  BytecodeVerifyOptions options;options.max_locals_per_code=gpu_detail::MAX_LOCALS;options.max_stack_depth=gpu_detail::MAX_STACK;
  const auto verified=verify_bytecode(impl_->programs.back(),options);
  phase_layout_require(bool(verified),"cold GPU executable verification failed");
  impl_->fixed_stack_bounds.push_back(verified.verified.max_stack_depth);
  impl_->views_proven=impl_->views_proven && gpu_detail::view_program_supported(impl_->programs.back(),impl_->input_types);
  impl_->direct_root_proven=impl_->direct_root_proven && gpu_detail::direct_region_root_supported(impl_->programs.back());
 }
 impl_->stack_bounds=impl_->fixed_stack_bounds;
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
 impl_->fitness.reserve(input.size());impl_->stats.reserve(input.size());impl_->code.reserve(rows*code_capacity);impl_->constants.reserve(rows*capacity);
 impl_->compiled.reserve(rows);impl_->code_offsets.reserve(rows+1);impl_->constant_offsets.reserve(rows+1);
 if(std::getenv("GAGP_OWNED_PHASE_PACKED"))impl_->initialize_host_packed();
 checked(cudaEventCreate(&impl_->start));checked(cudaEventCreate(&impl_->stop));
}
NativePhasePopulation::~NativePhasePopulation() {
 if(!impl_)return;int previous=-1;const bool known=cudaGetDevice(&previous)==cudaSuccess;
 if(known && impl_->device>=0 && previous!=impl_->device)cudaSetDevice(impl_->device);
 impl_.reset();if(known && previous>=0)cudaSetDevice(previous);
}
const std::vector<BytecodeProgram>& NativePhasePopulation::programs()const{impl_->require_device();impl_->materialize_programs();return impl_->programs;}
FitnessEvalResult NativePhasePopulation::evaluate(FitnessSessionGpu& session,bool diagnostics)const {
 impl_->require_device();
 if(!std::getenv("GAGP_OWNED_PHASE_PACK"))return session.eval_programs(programs(),diagnostics);
 const bool capabilities=std::getenv("GAGP_OWNED_PHASE_CAPS") && impl_->views_proven;
 const gpu_detail::OwnedGpuPrograms owned(impl_->programs,impl_->stack_bounds,
     capabilities?&impl_->input_types:nullptr,capabilities && impl_->direct_root_proven,
     impl_->host_packed.get(),[this]{impl_->materialize_programs();});
 return session.eval_owned_programs(owned,diagnostics);
}
const char* NativePhasePopulation::profile_name()const{return impl_->counted_sites?"native-gpu-phase-v2-counted":"native-gpu-phase-v1";}
std::size_t NativePhasePopulation::device_bytes()const{return std::max(impl_->bytes(),impl_->peak_transport_bytes);}
ReproductionStats NativePhasePopulation::reproduce(const std::vector<double>& completed_fitness,int pressure,double probability,std::uint64_t seed) {
 impl_->require_device();phase_layout_require(completed_fitness.size()==std::size_t(impl_->population) && pressure>0 && pressure<=impl_->population &&
   std::isfinite(probability) && probability>=0 && probability<=1,"selection/mutation contract");
 for(double value:completed_fitness)phase_layout_require(!std::isnan(value),"uncanonical fitness");
 impl_->healthy=false;ReproductionStats timing;const auto upload=Clock::now();impl_->fitness.upload(completed_fitness);timing.upload_ms=elapsed(upload);
 const bool counted=impl_->counted_sites;
 int threads=64;
 if(const char* setting=std::getenv("GAGP_PHASE_REPRO_THREADS")){threads=std::atoi(setting);phase_layout_require(threads==1 || threads==2 || threads==4 || threads==8 || threads==16 || threads==32 || threads==64 || threads==128,"phase reproduction block size");}
 const int p=impl_->population,k=impl_->slot_count,available=impl_->layout.budget.max_nodes-impl_->layout.fixed_nodes;
 checked(cudaEventRecord(impl_->start));
 crossover_population<<<(p+threads-1)/threads,threads>>>(pointers(*impl_->current),pointers(*impl_->crossed),impl_->slots.data,k,p,available,
    impl_->fitness.data,pressure,seed,impl_->stats.data,counted);checked(cudaGetLastError());
 const bool fused=std::getenv("GAGP_GPU_FUSED_DONOR_SITES")!=nullptr;
 if(impl_->inplace_mutation) {
  if(fused)mutate_population<true,true><<<(p+threads-1)/threads,threads>>>(pointers(*impl_->crossed),pointers(*impl_->next),pointers(*impl_->scratch),impl_->slots.data,k,p,available,
      probability,seed,impl_->stats.data,counted);
  else mutate_population<true,false><<<(p+threads-1)/threads,threads>>>(pointers(*impl_->crossed),pointers(*impl_->next),pointers(*impl_->scratch),impl_->slots.data,k,p,available,
      probability,seed,impl_->stats.data,counted);
 } else {
  if(fused)mutate_population<false,true><<<(p+threads-1)/threads,threads>>>(pointers(*impl_->crossed),pointers(*impl_->next),pointers(*impl_->scratch),impl_->slots.data,k,p,available,
      probability,seed,impl_->stats.data,counted);
  else mutate_population<false,false><<<(p+threads-1)/threads,threads>>>(pointers(*impl_->crossed),pointers(*impl_->next),pointers(*impl_->scratch),impl_->slots.data,k,p,available,
      probability,seed,impl_->stats.data,counted);
 }
 checked(cudaGetLastError());
 auto& offspring=impl_->inplace_mutation?impl_->crossed:impl_->next;
 compare_offspring<<<(p+threads-1)/threads,threads>>>(pointers(*impl_->current),pointers(*offspring),p,k,impl_->stats.data);checked(cudaGetLastError());
 checked(cudaEventRecord(impl_->stop));checked(cudaEventSynchronize(impl_->stop));float ms;checked(cudaEventElapsedTime(&ms,impl_->start,impl_->stop));timing.kernel_ms=ms;
 const auto stats=impl_->stats.read(p);
 for(const auto& one:stats) {
  if(one.error)throw std::runtime_error("GPU genotype variation error "+std::to_string(one.error));
  ++timing.variation.crossover_attempts;timing.variation.mutation_attempts+=one.mutation;
  timing.variation.contract_rejections+=one.crossover_rejected;
  timing.variation.changed_children+=one.changed;timing.variation.unchanged_children+=!one.changed;
  timing.variation.fallback_children+=!one.crossover;
 }
 impl_->current.swap(offspring);impl_->refresh_programs(&timing);impl_->healthy=true;return timing;
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
