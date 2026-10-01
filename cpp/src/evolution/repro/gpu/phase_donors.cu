#include "phase_donors.hpp"
#include "phase_grammar.hpp"
#include "internal.hpp"
#include <cuda_runtime.h>
#include <map>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <set>
#include <utility>

namespace gagp::evo::repro {
namespace {
void cuda_require(cudaError_t status) {if(status!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(status));}
template<class T> struct DeviceStorage {
 T* data=nullptr;std::size_t capacity=0;
 ~DeviceStorage(){if(data)cudaFree(data);}
 DeviceStorage()=default;DeviceStorage(const DeviceStorage&)=delete;
 void reserve(std::size_t n) {
   if(n<=capacity)return;
   T* next=nullptr;cuda_require(cudaMalloc(reinterpret_cast<void**>(&next),n*sizeof(T)));
   if(data)cudaFree(data);data=next;capacity=n;
 }
 void upload(const std::vector<T>& source){reserve(source.size());if(!source.empty())cuda_require(cudaMemcpy(data,source.data(),sizeof(T)*source.size(),cudaMemcpyHostToDevice));}
 void read(std::vector<T>& target,std::size_t n){target.resize(n);if(n)cuda_require(cudaMemcpy(target.data(),data,sizeof(T)*n,cudaMemcpyDeviceToHost));}
};
struct UploadedGrammar {
 HostPhaseGrammar host;
 DeviceStorage<PhaseExpression> expressions;DeviceStorage<PhaseProduction> productions;
 DeviceStorage<PhaseNonterminal> nonterminals;DeviceStorage<PhaseDomain> domains;
 DeviceStorage<Value> values;DeviceStorage<unsigned> minimum;
 explicit UploadedGrammar(HostPhaseGrammar model):host(std::move(model)) {
   expressions.upload(host.expressions);productions.upload(host.productions);nonterminals.upload(host.nonterminals);
   domains.upload(host.domains);values.upload(host.values);minimum.upload(host.minimum);
   std::vector<unsigned>().swap(host.minimum);
 }
 PhaseGrammarView view()const{return {expressions.data,int(host.expressions.size()),productions.data,int(host.productions.size()),
   nonterminals.data,int(host.nonterminals.size()),domains.data,int(host.domains.size()),values.data,int(host.values.size()),minimum.data,host.depth_count};}
};
struct Request {int offset,root,scope,depth,capacity;std::uint64_t seed;};
__global__ void construct(const PhaseGrammarView grammar,const Request* requests,int count,
 PhaseTreeNode* nodes,int* origins,Value* constants,PhaseGenerationResult* results) {
 const int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=count)return;const auto row=requests[i];
 results[i]=generate_phase(grammar,row.root,row.scope,row.depth,row.capacity,row.seed,
     nodes+row.offset,origins+row.offset,constants+row.offset);
}
struct CompactRequest { int source,node_offset,const_offset,nodes,constants; };
__global__ void compact_generated(const CompactRequest* requests,int count,
    const PhaseTreeNode* input_nodes,const int* input_origins,const Value* input_constants,
    PhaseTreeNode* nodes,int* origins,Value* constants) {
  const int row=blockIdx.x;if(row>=count)return;const auto r=requests[row];
  for(int i=threadIdx.x;i<r.nodes;i+=blockDim.x){nodes[r.node_offset+i]=input_nodes[r.source+i];origins[r.node_offset+i]=input_origins[r.source+i];}
  for(int i=threadIdx.x;i<r.constants;i+=blockDim.x)constants[r.const_offset+i]=input_constants[r.source+i];
}
std::size_t table_bytes(const HostPhaseGrammar& h) {
 return h.expressions.size()*sizeof(PhaseExpression)+h.productions.size()*sizeof(PhaseProduction)+
 h.nonterminals.size()*sizeof(PhaseNonterminal)+h.domains.size()*sizeof(PhaseDomain)+
 h.values.size()*sizeof(Value)+h.minimum.size()*sizeof(unsigned);
}
}
class GpuPhaseDonorSession {
 public:
 explicit GpuPhaseDonorSession(std::shared_ptr<const grammar::CompiledGrammar> g):grammar(std::move(g)) {
   if(!grammar)throw std::invalid_argument("GPU donor session requires grammar ownership");
 }
 std::shared_ptr<const grammar::CompiledGrammar> grammar;
 int device=-1;std::size_t table_storage=0;double setup_ms=0;
 std::map<unsigned,std::unique_ptr<UploadedGrammar>> profiles;
 DeviceStorage<Request> requests;DeviceStorage<PhaseTreeNode> nodes;
 DeviceStorage<int> origins;DeviceStorage<Value> constants;DeviceStorage<PhaseGenerationResult> results;
 DeviceStorage<CompactRequest> compact_requests;DeviceStorage<PhaseTreeNode> packed_nodes;
 DeviceStorage<int> packed_origins;DeviceStorage<Value> packed_constants;
 void initialize() {
   if(device>=0){int current;cuda_require(cudaGetDevice(&current));if(current!=device)throw std::logic_error("GPU donor session device changed");return;}
   std::string error;if(!initialize_gpu_repro_runtime(&error))throw std::runtime_error(error);
   cuda_require(cudaGetDevice(&device));
 }
 UploadedGrammar* profile(unsigned nt) {
   const auto found=profiles.find(nt);if(found!=profiles.end())return found->second.get();
   const auto start=std::chrono::steady_clock::now();
   struct Record { double& total;std::chrono::steady_clock::time_point start;
     ~Record(){total+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
   } record{setup_ms,start};
   HostPhaseGrammar compiled;
   try{compiled=compile_phase_grammar(*grammar,nt,std::min<unsigned>(64,grammar->search_limits().max_depth));}
   catch(const std::invalid_argument&){profiles.emplace(nt,nullptr);return nullptr;}
   constexpr std::size_t max_tables=64*1024*1024;
   const auto bytes=table_bytes(compiled);
   if(profiles.size()>=64 || bytes>max_tables-table_storage){profiles.emplace(nt,nullptr);return nullptr;}
   initialize();auto profile=std::make_unique<UploadedGrammar>(std::move(compiled));auto* pointer=profile.get();
   profiles.emplace(nt,std::move(profile));table_storage+=bytes;return pointer;
 }
};
std::shared_ptr<GpuPhaseDonorSession> make_gpu_phase_donor_session(std::shared_ptr<const grammar::CompiledGrammar> grammar) {
 return std::make_shared<GpuPhaseDonorSession>(std::move(grammar));
}
std::vector<std::optional<std::vector<ConstructedPhaseDonor>>> construct_gpu_phase_donors(
 GpuPhaseDonorSession& session,const grammar::CompiledGrammar& grammar,const std::vector<grammar::DonorPoolJob>& jobs,GpuPhaseDonorMetrics* metrics) {
 if(session.grammar.get()!=&grammar)throw std::invalid_argument("GPU donor session grammar owner mismatch");
 if(jobs.size()>65536)throw std::invalid_argument("GPU donor batch job capacity");
 const auto initial_setup=session.setup_ms;
 using Result=std::optional<std::vector<ConstructedPhaseDonor>>;std::vector<Result> output(jobs.size());
 struct Selected{std::size_t job,seed;};std::map<unsigned,std::vector<Selected>> groups;
 for(std::size_t i=0;i<jobs.size();++i) {
   const auto& job=jobs[i];const auto& site=job.site;
   if(job.seeds.empty())continue;
   if(job.seeds.size()>64)throw std::invalid_argument("GPU donor pool capacity");
   if(site.nonterminal>=grammar.nonterminals().size())throw std::invalid_argument("GPU donor unknown nonterminal");
   const auto& nt=grammar.nonterminals()[site.nonterminal];
   if(nt.mutation_entry!=grammar::kNoGrammarId || site.has_projected_allowance || site.category!=NodeCategory::Expression ||
       site.type!=nt.type || site.context!=nt.context || site.visible_environment.size()!=nt.scope.size() ||
       site.occurrence_binder_ids.empty() || site.occurrence_binder_ids.front().size()!=nt.scope.size() ||
       !site.replacement_budget.max_nodes || site.replacement_budget.max_nodes>1024 ||
       !site.replacement_budget.max_depth || site.replacement_budget.max_depth>64)continue;
   bool scope=true;std::set<int> ids;
   for(std::size_t j=0;j<nt.scope.size();++j) {
     const auto id=site.occurrence_binder_ids.front()[j];
     scope &= site.visible_environment[j].name==nt.scope[j].name && site.visible_environment[j].type==nt.scope[j].type &&
         id>=0 && id<std::numeric_limits<int>::max() && ids.insert(id).second;
   }
   if(!scope)continue;
   auto* profile=session.profile(site.nonterminal);if(!profile)continue;
   const auto depth=site.replacement_budget.max_depth;
   if(depth>=static_cast<unsigned>(profile->host.depth_count) || nt.generation_costs(grammar::GenerationStage::Mutation)[depth]>site.replacement_budget.max_nodes)continue;
   output[i].emplace(job.seeds.size());
   for(std::size_t seed=0;seed<job.seeds.size();++seed)groups[site.nonterminal].push_back({i,seed});
 }
 for(const auto& group:groups) {
   auto& uploaded=*session.profiles.at(group.first);const auto& selected=group.second;
   for(std::size_t begin=0;begin<selected.size();begin+=1024) {
     const auto count=std::min<std::size_t>(1024,selected.size()-begin);std::vector<Request> requests;requests.reserve(count);int total=0;
     for(std::size_t i=0;i<count;++i) {
       const auto key=selected[begin+i];const auto& job=jobs[key.job];const auto& site=job.site;
       requests.push_back({total,uploaded.host.root_expression,int(site.visible_environment.size()),int(site.replacement_budget.max_depth),int(site.replacement_budget.max_nodes),job.seeds[key.seed]});
       total+=site.replacement_budget.max_nodes;
     }
     const bool trace=std::getenv("GAGP_GPU_DONOR_TRACE")!=nullptr;
     const auto upload_begin=std::chrono::steady_clock::now();
     session.initialize();session.requests.upload(requests);session.nodes.reserve(total);session.origins.reserve(total);session.constants.reserve(total);session.results.reserve(count);
     const auto upload_end=std::chrono::steady_clock::now();
     cudaEvent_t event_begin=nullptr,event_end=nullptr;
     if(trace){cuda_require(cudaEventCreate(&event_begin));cuda_require(cudaEventCreate(&event_end));cuda_require(cudaEventRecord(event_begin));}
     construct<<<(count+63)/64,64>>>(uploaded.view(),session.requests.data,count,session.nodes.data,session.origins.data,session.constants.data,session.results.data);
     cuda_require(cudaGetLastError());
     float kernel_ms=0;
     if(trace){cuda_require(cudaEventRecord(event_end));cuda_require(cudaEventSynchronize(event_end));cuda_require(cudaEventElapsedTime(&kernel_ms,event_begin,event_end));cudaEventDestroy(event_begin);cudaEventDestroy(event_end);}
     const auto read_begin=std::chrono::steady_clock::now();
     std::vector<PhaseGenerationResult> results;std::vector<PhaseTreeNode> nodes;std::vector<int> origins;std::vector<Value> constants;
     session.results.read(results,count);
     std::vector<CompactRequest> compact;compact.reserve(count);int node_total=0,const_total=0;
     for(std::size_t i=0;i<count;++i) {
       const auto r=results[i];const auto row=requests[i];
       if(r.status!=PhaseCompileStatus::Ok || r.nodes<=0 || r.nodes>row.capacity || r.constants<0 || r.constants>r.nodes)
         throw std::runtime_error("GPU donor construction failed within an admitted feasible profile");
       compact.push_back({row.offset,node_total,const_total,r.nodes,r.constants});node_total+=r.nodes;const_total+=r.constants;
     }
     session.compact_requests.upload(compact);session.packed_nodes.reserve(node_total);session.packed_origins.reserve(node_total);session.packed_constants.reserve(const_total);
     compact_generated<<<count,128>>>(session.compact_requests.data,count,session.nodes.data,session.origins.data,session.constants.data,
         session.packed_nodes.data,session.packed_origins.data,session.packed_constants.data);
     cuda_require(cudaGetLastError());
     session.packed_nodes.read(nodes,node_total);session.packed_origins.read(origins,node_total);session.packed_constants.read(constants,const_total);
     const auto read_end=std::chrono::steady_clock::now();
     std::size_t generated_nodes=0;
     for(std::size_t i=0;i<count;++i) {
       const auto key=selected[begin+i];const auto& job=jobs[key.job];const auto& r=results[i];
       generated_nodes+=r.nodes;const auto row=requests[i];
       if(r.status!=PhaseCompileStatus::Ok || r.nodes<=0 || r.nodes>row.capacity || r.constants<0 || r.constants>r.nodes)
         throw std::runtime_error("GPU donor construction failed within an admitted feasible profile");
       auto& donor=output[key.job]->at(key.seed);auto& ast=donor.fragment;
       const auto packed=compact[i];
       ast.consts.assign(constants.begin()+packed.const_offset,constants.begin()+packed.const_offset+r.constants);
       std::vector<unsigned> pending{1};unsigned max_depth=0;
       for(int j=0;j<r.nodes;++j) {
         while(!pending.empty() && !pending.back())pending.pop_back();
         if(pending.empty())throw std::runtime_error("GPU donor trailing prefix node");
         max_depth=std::max<unsigned>(max_depth,pending.size());--pending.back();
         const int origin=origins[packed.node_offset+j];
         if(origin<0 || static_cast<std::size_t>(origin)>=grammar.expressions().size())throw std::runtime_error("GPU donor expression index");
         const auto& e=grammar.expressions()[origin];const auto& model=uploaded.host.expressions[origin];const auto node=nodes[packed.node_offset+j];
         if(node.kind!=model.node.kind || node.opcode!=model.node.opcode || node.arity!=model.node.arity ||
             node.fuel!=model.node.fuel || node.merge_fuel!=model.node.merge_fuel)throw std::runtime_error("GPU donor node contract");
         AstNode native{static_cast<NodeKind>(model.native_kind)};
         if(e.kind==grammar::ExpressionKind::Constant) {
           if(node.operand<0 || node.operand>=r.constants)throw std::runtime_error("GPU donor constant index");native.i0=node.operand;
         } else if(e.kind==grammar::ExpressionKind::Bound) {
           if(node.operand<0 || node.operand>=row.scope)throw std::runtime_error("GPU donor scope index");native.i0=job.site.occurrence_binder_ids.front()[node.operand];
         } else if(e.kind!=grammar::ExpressionKind::Primitive || node.operand!=model.node.operand)throw std::runtime_error("GPU donor expression contract");
         if(node.arity)pending.push_back(node.arity);
         if(!e.fuel_charges.empty())ast.fuel_specs.push_back({ast.nodes.size(),e.fuel_charges});
         ast.nodes.push_back(native);
         grammar::NodeOrigin record;record.expression=origin;record.logical_instance=j;donor.origins.push_back(record);
       }
       for(auto remaining:pending)if(remaining)throw std::runtime_error("GPU donor missing prefix children");
       if(max_depth>static_cast<unsigned>(row.depth))throw std::runtime_error("GPU donor depth overflow");donor.depth=max_depth;
     }
     if(trace) {
       const auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
       std::cerr<<"GPU_DONOR_BATCH root="<<group.first<<" donors="<<count<<" nodes="<<generated_nodes<<" slots="<<total
           <<" upload_ms="<<ms(upload_begin,upload_end)<<" kernel_ms="<<kernel_ms<<" read_ms="<<ms(read_begin,read_end)
           <<" assemble_ms="<<ms(read_end,std::chrono::steady_clock::now())<<"\n";
     }
   }
 }
 if(metrics) {
   metrics->setup_ms=session.setup_ms-initial_setup;
   metrics->device_bytes=session.table_storage+session.requests.capacity*sizeof(Request)+session.nodes.capacity*sizeof(PhaseTreeNode)+
       session.origins.capacity*sizeof(int)+session.constants.capacity*sizeof(Value)+session.results.capacity*sizeof(PhaseGenerationResult)+
       session.compact_requests.capacity*sizeof(CompactRequest)+session.packed_nodes.capacity*sizeof(PhaseTreeNode)+
       session.packed_origins.capacity*sizeof(int)+session.packed_constants.capacity*sizeof(Value);
 }
 return output;
}
} // namespace gagp::evo::repro
