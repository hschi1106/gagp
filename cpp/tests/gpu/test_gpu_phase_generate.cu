#include <cuda_runtime.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "../../src/evolution/repro/gpu/phase_grammar.hpp"
#include "../../src/evolution/repro/gpu/phase_donors.hpp"
#include "../../src/evolution/repro/prep_internal.hpp"
#include "../../src/evolution/repro/constant_prep.hpp"
namespace {
using namespace gagp;using namespace gagp::evo;using namespace gagp::evo::repro;
void require(bool b,const std::string& why){if(!b)throw std::runtime_error(why);}
void check(cudaError_t e){if(e!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(e));}
template<class T> struct Device {
 T* data=nullptr;std::size_t count;
 explicit Device(std::size_t n):count(n){check(cudaMalloc(reinterpret_cast<void**>(&data),sizeof(T)*std::max<std::size_t>(1,n)));}
 ~Device(){cudaFree(data);}Device(const Device&)=delete;
 void upload(const std::vector<T>& x){require(x.size()==count,"upload capacity");if(count)check(cudaMemcpy(data,x.data(),sizeof(T)*count,cudaMemcpyHostToDevice));}
 std::vector<T> read(){std::vector<T>x(count);if(count)check(cudaMemcpy(x.data(),data,sizeof(T)*count,cudaMemcpyDeviceToHost));return x;}
};
__global__ void generate_many(PhaseGrammarView g,int root,int scope,int depth,int capacity,int count,
 PhaseTreeNode* nodes,int* origins,Value* values,PhaseGenerationResult* results) {
 const int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=count)return;
 results[i]=generate_phase(g,root,scope,depth,capacity,UINT64_C(1982371)+i,nodes+i*capacity,origins+i*capacity,values+i*capacity);
}
std::string scalar_definition(){return R"({"format_version":"grammar-definition-v2",
 "entry":{"nonterminal":"Int","type":"Int"},"search_limits":{"max_nodes":132,"max_depth":20},"execution_limits":{"fuel":10000},
 "nonterminals":[{"id":"Int","type":"Int","scope":[],"alternatives":[
 {"id":"literal","weight":1,"expression":{"constant":{"type":"Int","range":["-9223372036854775808","9223372036854775807"]}}},
 {"id":"sum","weight":2,"expression":{"signature":"add(Int,Int)->Int","args":[{"ref":"Int"},{"ref":"Int"}]}},
 {"id":"choose","weight":1,"expression":{"signature":"if(Bool,Int,Int)->Int","args":[{"ref":"Bool"},{"ref":"Int"},{"ref":"Int"}]}}]},
 {"id":"Bool","type":"Bool","scope":[],"alternatives":[
 {"id":"literal","weight":1,"expression":{"constant":{"type":"Bool","values":[true,false]}}},
 {"id":"compare","weight":1,"expression":{"signature":"lt(Int,Int)->Bool","args":[{"ref":"Int"},{"ref":"Int"}]}}]}]})";}
void check_native_preparation() {
 const auto owner=std::make_shared<const grammar::CompiledGrammar>(grammar::compile_grammar(grammar::parse_definition(scalar_definition())));
 grammar::VariationContext context(owner);std::vector<ProgramGenome> parents;
 constexpr int population=32;
 for(int i=0;i<population;++i)parents.push_back(grammar::generate_derivation(*owner,i+1).genome);
 const auto owned=grammar::variation_detail::OwnedScalarPopulation::create(parents,context);
 auto session=make_gpu_phase_donor_session(owner);
 GpuReproConfig cfg;cfg.population_size=population;cfg.max_nodes=owner->search_limits().max_nodes;
 cfg.max_expr_depth=owner->search_limits().max_depth;cfg.candidates_per_program=8;cfg.donor_pool_size_per_site=4;
 cfg.compiled_pass=CompiledVariationPass::Mutation;cfg.seed=8129;cfg.mutation_ratio=1;cfg.mutation_subtree_ratio=1;
 const auto result=preprocess_selected_population(owned->genomes(),cfg,context,prepare_constant_mutation_domains(owner),true,owned.get(),session.get());
 require(result.gpu_donor_generated==population*4 && result.gpu_donor_fallback==0,
     "selected mutation preparation failed to execute all deferred GPU donor requests");
 require(result.donor_pool.size()==population*4,"native preparation lost GPU donors");
 for(const auto& row:result.candidates) {
   int prepared=0;for(const auto& site:row)prepared+=site.donor_count;
   require(prepared==4,"native preparation left a parent's mutation site without donors");
 }
}
void run_root(const std::shared_ptr<const grammar::CompiledGrammar>& owner,unsigned root,const HostPhaseGrammar& host) {
 const auto& grammar=*owner;
 const auto& nt=grammar.nonterminals().at(root);constexpr int count=256;
 const int capacity=std::min<unsigned>(128,grammar.search_limits().max_nodes-4);
 const int depth=std::min<unsigned>(8,grammar.search_limits().max_depth-3);
 Device<PhaseExpression> e(host.expressions.size());Device<PhaseProduction> p(host.productions.size());
 Device<PhaseNonterminal> n(host.nonterminals.size());Device<PhaseDomain> d(host.domains.size());Device<Value> v(host.values.size());Device<unsigned> m(host.minimum.size());
 Device<PhaseTreeNode> nodes(count*capacity);Device<int> origins(count*capacity);Device<Value> values(count*capacity);Device<PhaseGenerationResult> results(count);
 e.upload(host.expressions);p.upload(host.productions);n.upload(host.nonterminals);d.upload(host.domains);v.upload(host.values);m.upload(host.minimum);
 PhaseGrammarView view{e.data,int(e.count),p.data,int(p.count),n.data,int(n.count),d.data,int(d.count),v.data,int(v.count),m.data,host.depth_count};
 auto launch=[&](int scope,int limit_depth,int limit_capacity){generate_many<<<(count+63)/64,64>>>(view,host.root_expression,scope,limit_depth,limit_capacity,count,nodes.data,origins.data,values.data,results.data);check(cudaGetLastError());check(cudaDeviceSynchronize());};
 const auto begin=std::chrono::steady_clock::now();launch(nt.scope.size(),depth,capacity);
 const double cold_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
 cudaEvent_t start,stop;check(cudaEventCreate(&start));check(cudaEventCreate(&stop));std::vector<float> warm;
 for(int i=0;i<3;++i){check(cudaEventRecord(start));launch(nt.scope.size(),depth,capacity);check(cudaEventRecord(stop));check(cudaEventSynchronize(stop));float ms;check(cudaEventElapsedTime(&ms,start,stop));warm.push_back(ms);}
 cudaEventDestroy(start);cudaEventDestroy(stop);
 const auto output=results.read();const auto tree=nodes.read();const auto origin=origins.read();const auto constants=values.read();
 grammar::GenerationRequest request{root,nt.type,nt.scope,grammar.search_limits(),grammar::GenerationStage::Mutation};
 grammar::GenerationFrame frame;for(std::size_t i=0;i<nt.scope.size();++i)frame.binder_ids.push_back(100+int(i));
 std::set<std::string> unique;std::size_t node_total=0;
 for(int i=0;i<count;++i) {
   const auto r=output[i];require(r.status==PhaseCompileStatus::Ok,"generation failed for supported feasible root "+nt.stable_id+" seed "+std::to_string(i));
   require(r.nodes>0 && r.nodes<=capacity && r.constants>=0 && r.constants<=r.nodes,"output capacity");
   ProgramGenome genome;auto& ast=genome.ast;ast.nodes={{NodeKind::PROGRAM},{NodeKind::BLOCK_CONS},{NodeKind::RETURN}};
   ast.consts.assign(constants.begin()+i*capacity,constants.begin()+i*capacity+r.constants);
   for(int j=0;j<r.nodes;++j) {
     const auto& expression=grammar.expressions().at(origin[i*capacity+j]);const auto& model=host.expressions.at(origin[i*capacity+j]);const auto node=tree[i*capacity+j];
     AstNode native{static_cast<NodeKind>(model.native_kind)};
     if(expression.kind==grammar::ExpressionKind::Constant)native.i0=node.operand;
     else if(expression.kind==grammar::ExpressionKind::Bound)native.i0=frame.binder_ids.at(node.operand);
     if(!expression.fuel_charges.empty())ast.fuel_specs.push_back({ast.nodes.size(),expression.fuel_charges});
     ast.nodes.push_back(native);
   }
   ast.nodes.push_back({NodeKind::BLOCK_NIL});
   // Independent canonical membership checks scope, constants, productions,
   // envelope, budgets and native typing; metadata from CUDA is not trusted.
   grammar::require_membership_in_frame(grammar,genome,request,frame);
   const auto admitted=grammar::reconstruct_derivation_in_frame(grammar,genome,request,frame);
   require(admitted.lowered_instructions>0,"independent lowering missing");
   unique.insert(ast_cache_key(ast));node_total+=r.nodes;
 }
 // Exercise the exact native preparation API and its run-owned table lifetime.
 auto session=make_gpu_phase_donor_session(owner);
 grammar::DonorPoolJob job;job.site.nonterminal=root;job.site.type=nt.type;job.site.context=nt.context;
 job.site.visible_environment=nt.scope;job.site.occurrence_binder_ids={frame.binder_ids};
 job.site.replacement_budget={static_cast<unsigned>(capacity),static_cast<unsigned>(depth)};job.seeds={17,97,893};
 GpuPhaseDonorMetrics first_metrics,second_metrics;
 const auto first=construct_gpu_phase_donors(*session,grammar,{job},&first_metrics);
 const auto second=construct_gpu_phase_donors(*session,grammar,{job},&second_metrics);
 require(first[0] && second[0] && first[0]->size()==job.seeds.size(),"native GPU donor API declined admitted request");
 require(first_metrics.setup_ms>0 && second_metrics.setup_ms==0 && first_metrics.device_bytes==second_metrics.device_bytes,
     "run-owned grammar tables were rebuilt or storage grew on identical requests");
 for(std::size_t i=0;i<first[0]->size();++i) {
   const auto& donor=first[0]->at(i);
   require(ast_cache_key(donor.fragment)==ast_cache_key(second[0]->at(i).fragment),"GPU donor seed replay changed");
   ProgramGenome exported;exported.ast=donor.fragment;
   exported.ast.nodes.insert(exported.ast.nodes.begin(),{{NodeKind::PROGRAM},{NodeKind::BLOCK_CONS},{NodeKind::RETURN}});
   exported.ast.nodes.push_back({NodeKind::BLOCK_NIL});for(auto& fuel:exported.ast.fuel_specs)fuel.node_index+=3;
   grammar::require_membership_in_frame(grammar,exported,request,frame);
 }
 const auto copied_grammar=grammar;
 bool wrong_owner=false;
 try{(void)construct_gpu_phase_donors(*session,copied_grammar,{job});}catch(const std::invalid_argument&){wrong_owner=true;}
 require(wrong_owner,"equal-content foreign grammar reused GPU donor owner");
 auto unsupported_job=job;unsupported_job.site.replacement_budget.max_nodes=1025;
 require(!construct_gpu_phase_donors(*session,grammar,{unsupported_job})[0],"unsupported capacity did not request fallback");
 unsupported_job=job;unsupported_job.site.type=RType::Invalid;
 require(!construct_gpu_phase_donors(*session,grammar,{unsupported_job})[0],"wrong site type admitted");
 if(!nt.scope.empty()) {
   unsupported_job=job;unsupported_job.site.occurrence_binder_ids[0][0]=-1;
   require(!construct_gpu_phase_donors(*session,grammar,{unsupported_job})[0],"unset lexical binding admitted");
 }
 // Rejected requests cannot become valid output. Exercise the actual device
 // guard, including a zero capacity that shares the same output base pointer.
 launch(nt.scope.size(),0,capacity);for(const auto& r:results.read())require(r.status!=PhaseCompileStatus::Ok,"zero depth accepted");
 launch(9,depth,capacity);for(const auto& r:results.read())require(r.status!=PhaseCompileStatus::Ok,"scope overflow accepted");
 launch(nt.scope.size(),depth,0);for(const auto& r:results.read())require(r.status!=PhaseCompileStatus::Ok,"zero capacity accepted");
 std::cout<<"{\"root\":\""<<nt.stable_id<<"\",\"generated\":"<<count<<",\"unique\":"<<unique.size()<<",\"mean_nodes\":"<<double(node_total)/count
   <<",\"cold_ms\":"<<cold_ms<<",\"warm_ms\":["<<warm[0]<<","<<warm[1]<<","<<warm[2]<<"]}"<<std::endl;
}
}
int main(int argc,char** argv){try{
 require(argc<=3,"usage: gagp_test_gpu_phase_generate [GRAMMAR_JSON [EXPECTED_SUPPORTED_ROOTS]]");
 const auto definition=argc>=2?grammar::load_definition(argv[1]):grammar::parse_definition(scalar_definition());
 const auto owner=std::make_shared<const grammar::CompiledGrammar>(grammar::compile_grammar(definition));
 const auto& grammar=*owner;
 FitnessSessionGpu session;require(session.init({{}},{Value::from_int(0)},1,1,1.0).ok,"CUDA initialization");
 check_native_preparation();
 int admitted=0,unsupported=0;
 for(const auto& nt:grammar.nonterminals()) {
   HostPhaseGrammar profile;
   try{profile=compile_phase_grammar(grammar,nt.id,std::min<unsigned>(32,grammar.search_limits().max_depth));}
   catch(const std::invalid_argument& error){++unsupported;std::cout<<"unsupported "<<nt.stable_id<<": "<<error.what()<<std::endl;continue;}
   run_root(owner,nt.id,profile);++admitted;
 }
 require(admitted>0,"no supported grammar root tested");
 if(argc==3)require(admitted==std::stoi(argv[2]),"required root coverage changed");
 if(argc==1)require(admitted==int(grammar.nonterminals().size()),"default root unexpectedly declined");
 std::cout<<"checked_roots="<<admitted<<" unsupported_roots="<<unsupported<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
