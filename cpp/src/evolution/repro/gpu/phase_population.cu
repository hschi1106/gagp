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
  expressions.upload(h.expressions);productions.upload(h.productions);nonterminals.upload(h.nonterminals);
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
__device__ int tournament(const double* fitness,int population,int pressure,DGrammarRandom& random) {
 int best=random.bounded(population);
 for(int i=1;i<pressure;++i){const int candidate=random.bounded(population);if(fitness[candidate]>fitness[best])best=candidate;}
 return best;
}
struct Site {int row=-1,node=-1;};
__device__ Site choose_site(TreePointers trees,int program,int slots,DGrammarRandom& random,int nt=-1,int scope=-1) {
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
    int slot_count,int population,int max_variable_nodes,const double* fitness,int pressure,std::uint64_t seed,OperatorStats* stats) {
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
  const auto destination=choose_site(input,parent,slot_count,random);if(destination.node<0){stat.error=2;return;}
  const auto a=input.view(destination.row);const auto contract=a.sites[destination.node];
  const auto source=choose_site(input,donor,slot_count,random,contract.nonterminal,contract.scope);
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
__global__ void mutate_population(TreePointers input,TreePointers output,TreePointers scratch,const DeviceSlot* slots,
    int slot_count,int population,int max_variable_nodes,double probability,std::uint64_t seed,OperatorStats* stats) {
 const int child=blockIdx.x*blockDim.x+threadIdx.x;if(child>=population)return;auto& stat=stats[child];if(stat.error)return;
 int total=0;
 for(int k=0;k<slot_count;++k){const int row=child*slot_count+k;if(!valid(input.lengths[row])){stat.error=4;return;}total+=input.lengths[row].nodes;copy_tree(input,row,output,row);}
 DGrammarRandom random(seed ^ (UINT64_C(0x124ab512bf01)+child));
 if(static_cast<double>(random.next()>>11)*0x1.0p-53>=probability)return;
 const auto destination=choose_site(input,child,slot_count,random);if(destination.node<0){stat.error=5;return;}
 const auto a=input.view(destination.row);const int end=phase_subtree_end(a,destination.node),depth=phase_node_depth(a,destination.node);
 if(end<0 || depth<0){stat.error=6;return;}
 const auto site=a.sites[destination.node];const auto& slot=slots[destination.row%slot_count];auto tmp=scratch.output(child);
 const int available=max_variable_nodes-(total-(end-destination.node));
 auto generated=generate_phase(slot.grammar,site.entry_expression,site.parent_scope,slot.max_depth-depth+1,
     min(capacity,available),random.next(),tmp.nodes,tmp.origins,tmp.constants);
 if(generated.status!=PhaseCompileStatus::Ok || !annotate_phase_tree(slot.grammar,site.entry_expression,site.parent_scope,
       tmp.nodes,tmp.origins,generated.nodes,tmp.sites)){stat.error=7;return;}
 scratch.lengths[child]=generated;
 auto r=splice_phase_tree(a,destination.node,scratch.view(child),0,output.output(destination.row),slot.max_depth);
 if(r.status!=PhaseCompileStatus::Ok){stat.error=8;return;}
 output.lengths[destination.row]=r;stat.mutation=1;
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
__global__ void offsets_kernel(const PhaseCompileResult* compiled,int rows,int* code_offsets,int* constant_offsets) {
 if(blockIdx.x || threadIdx.x)return;code_offsets[0]=0;constant_offsets[0]=0;
 for(int i=0;i<rows;++i){const auto r=compiled[i];if(r.status!=PhaseCompileStatus::Ok){code_offsets[rows]=-1;constant_offsets[rows]=-1;return;}
  code_offsets[i+1]=code_offsets[i]+r.code_count;constant_offsets[i+1]=constant_offsets[i]+r.constant_count;}
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
 NativePhaseLayout layout;int population=0,slot_count=0,device=-1;bool healthy=true;
 std::vector<std::unique_ptr<UploadedProfile>> profiles;Buffer<DeviceSlot> slots;
 std::unique_ptr<Trees> current,next,crossed,scratch;
 Buffer<double> fitness;Buffer<OperatorStats> stats;
 Buffer<gpu_detail::DInstr> code,packed_code;Buffer<Value> constants,packed_constants;
 Buffer<PhaseCompileResult> compiled;Buffer<int> code_offsets,constant_offsets;
 std::vector<BytecodeProgram> programs;cudaEvent_t start=nullptr,stop=nullptr;
 explicit Impl(std::shared_ptr<const grammar::CompiledGrammar> g):grammar(std::move(g)){}
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
  offsets_kernel<<<1,1>>>(compiled.data,rows,code_offsets.data,constant_offsets.data);checked(cudaGetLastError());
  checked(cudaEventRecord(stop));checked(cudaEventSynchronize(stop));float ms;checked(cudaEventElapsedTime(&ms,start,stop));
  if(timing)timing->kernel_ms+=ms;
  const auto begin=Clock::now();auto offsets=code_offsets.read(rows+1),values=constant_offsets.read(rows+1);
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
  for(int p=0;p<population;++p)for(int k=0;k<slot_count;++k) {
   const int row=p*slot_count+k;auto& program=phase_at(programs[p].bounded_region_segments[0],layout.slots[k].ordinal).program;
   program.code.clear();program.instruction_fuel.clear();program.code.reserve(offsets[row+1]-offsets[row]);program.instruction_fuel.reserve(offsets[row+1]-offsets[row]);
   for(int i=offsets[row];i<offsets[row+1];++i){const auto x=compacted_code[i];program.code.push_back({static_cast<Opcode>(x.op),x.a,x.b,bool(x.flags&1),bool(x.flags&2)});program.instruction_fuel.push_back(x.fuel);}
   program.consts.assign(compacted_constants.begin()+values[row],compacted_constants.begin()+values[row+1]);
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
 std::string error;if(!initialize_gpu_repro_runtime(&error))throw std::runtime_error(error);checked(cudaGetDevice(&impl_->device));
 impl_->population=input.size();impl_->layout=import_native_phase_layout(*impl_->grammar,input.front());
 const auto capability=gpu_detail::pack_programs_with_shared_case_count({impl_->layout.executable},0,0);
 phase_layout_require(capability.metas.size()==1 && capability.metas.front().is_valid,"fixed executable requires GPU fallback");
 impl_->slot_count=impl_->layout.slots.size();phase_layout_require(impl_->slot_count>0 && impl_->slot_count<=8,"phase count capacity");
 const auto key=skeleton_key(impl_->layout);const std::size_t rows=input.size()*impl_->slot_count;
 const std::size_t tree_bytes=capacity*(sizeof(PhaseTreeNode)+sizeof(int)+sizeof(PhaseGeneSite)+sizeof(Value))+sizeof(PhaseGenerationResult);
 const std::size_t estimated=(3*rows+input.size())*tree_bytes+rows*(code_capacity*sizeof(gpu_detail::DInstr)+capacity*sizeof(Value));
 phase_layout_require(estimated<=std::size_t(12)*1024*1024*1024,"resident genotype workspace capacity requires native fallback");
 // Persistent buffers are bounded by population × grammar capacity; no retained
 // ancestral ASTs, intern tables or generation history exist in this owner.
 impl_->current=std::make_unique<Trees>(rows);impl_->next=std::make_unique<Trees>(rows);impl_->crossed=std::make_unique<Trees>(rows);impl_->scratch=std::make_unique<Trees>(input.size());
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
 // Upload live imports only, avoiding full-capacity host staging arrays.
 for(std::size_t p=0;p<input.size();++p) {
  auto imported=p==0?impl_->layout:import_native_phase_layout(*impl_->grammar,input[p]);
  phase_layout_require(imported.slots.size()==impl_->layout.slots.size() && skeleton_key(imported)==key,"mixed fixed skeleton requires native fallback");
  for(int k=0;k<impl_->slot_count;++k) {
   const auto& a=imported.slots[k];const auto& b=impl_->layout.slots[k];
   phase_layout_require(a.nonterminal==b.nonterminal && a.ordinal==b.ordinal && a.binders==b.binders && a.max_depth==b.max_depth,"mixed phase contract requires fallback");
   const int row=p*impl_->slot_count+k;const auto& gene=imported.genes[k];auto& l=lengths[row];l.status=PhaseCompileStatus::Ok;l.nodes=gene.nodes.size();l.constants=gene.constants.size();
   phase_layout_require(l.nodes>0 && l.nodes<=capacity && l.constants<=l.nodes,"imported phase capacity");
   checked(cudaMemcpy(impl_->current->nodes.data+std::size_t(row)*capacity,gene.nodes.data(),gene.nodes.size()*sizeof(PhaseTreeNode),cudaMemcpyHostToDevice));
   checked(cudaMemcpy(impl_->current->origins.data+std::size_t(row)*capacity,gene.origins.data(),gene.origins.size()*sizeof(int),cudaMemcpyHostToDevice));
   if(!gene.constants.empty())checked(cudaMemcpy(impl_->current->constants.data+std::size_t(row)*capacity,gene.constants.data(),gene.constants.size()*sizeof(Value),cudaMemcpyHostToDevice));
  }
  // Bind root and capture locals to the actual evaluator input order.
  impl_->programs.push_back(compile_for_eval(input[p],input_names));
 }
 impl_->current->lengths.upload(lengths);
 annotate_import<<<(rows+63)/64,64>>>(pointers(*impl_->current),impl_->slots.data,rows,impl_->slot_count);checked(cudaGetLastError());
 for(const auto& r:impl_->current->lengths.read(rows))phase_layout_require(valid(r),"import site annotation failed");
 impl_->fitness.reserve(input.size());impl_->stats.reserve(input.size());impl_->code.reserve(rows*code_capacity);impl_->constants.reserve(rows*capacity);
 impl_->compiled.reserve(rows);impl_->code_offsets.reserve(rows+1);impl_->constant_offsets.reserve(rows+1);
 checked(cudaEventCreate(&impl_->start));checked(cudaEventCreate(&impl_->stop));
}
NativePhasePopulation::~NativePhasePopulation() {
 if(!impl_)return;int previous=-1;const bool known=cudaGetDevice(&previous)==cudaSuccess;
 if(known && impl_->device>=0 && previous!=impl_->device)cudaSetDevice(impl_->device);
 impl_.reset();if(known && previous>=0)cudaSetDevice(previous);
}
const std::vector<BytecodeProgram>& NativePhasePopulation::programs()const{impl_->require_device();return impl_->programs;}
std::size_t NativePhasePopulation::device_bytes()const{return impl_->bytes();}
ReproductionStats NativePhasePopulation::reproduce(const std::vector<double>& completed_fitness,int pressure,double probability,std::uint64_t seed) {
 impl_->require_device();phase_layout_require(completed_fitness.size()==std::size_t(impl_->population) && pressure>0 && pressure<=impl_->population &&
   std::isfinite(probability) && probability>=0 && probability<=1,"selection/mutation contract");
 for(double value:completed_fitness)phase_layout_require(!std::isnan(value),"uncanonical fitness");
 impl_->healthy=false;ReproductionStats timing;const auto upload=Clock::now();impl_->fitness.upload(completed_fitness);timing.upload_ms=elapsed(upload);
 const int p=impl_->population,k=impl_->slot_count,available=impl_->layout.budget.max_nodes-impl_->layout.fixed_nodes;
 checked(cudaEventRecord(impl_->start));
 crossover_population<<<(p+63)/64,64>>>(pointers(*impl_->current),pointers(*impl_->crossed),impl_->slots.data,k,p,available,
    impl_->fitness.data,pressure,seed,impl_->stats.data);checked(cudaGetLastError());
 mutate_population<<<(p+63)/64,64>>>(pointers(*impl_->crossed),pointers(*impl_->next),pointers(*impl_->scratch),impl_->slots.data,k,p,available,
    probability,seed,impl_->stats.data);checked(cudaGetLastError());
 compare_offspring<<<(p+63)/64,64>>>(pointers(*impl_->current),pointers(*impl_->next),p,k,impl_->stats.data);checked(cudaGetLastError());
 checked(cudaEventRecord(impl_->stop));checked(cudaEventSynchronize(impl_->stop));float ms;checked(cudaEventElapsedTime(&ms,impl_->start,impl_->stop));timing.kernel_ms=ms;
 const auto stats=impl_->stats.read(p);
 for(const auto& one:stats) {
  if(one.error)throw std::runtime_error("GPU genotype variation error "+std::to_string(one.error));
  ++timing.variation.crossover_attempts;timing.variation.mutation_attempts+=one.mutation;
  timing.variation.contract_rejections+=one.crossover_rejected;
  timing.variation.changed_children+=one.changed;timing.variation.unchanged_children+=!one.changed;
  timing.variation.fallback_children+=!one.crossover;
 }
 impl_->current.swap(impl_->next);impl_->refresh_programs(&timing);impl_->healthy=true;return timing;
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
 auto result=export_native_phase_layout(*impl_->grammar,impl_->layout,genes);
 result.derivation=std::make_shared<const grammar::DerivationMetadata>(grammar::reconstruct_derivation(*impl_->grammar,result));
 return result;
}
} // namespace gagp::evo::repro
