#include <cuda_runtime.h>
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/ast_verify.hpp"
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "../../src/evolution/repro/gpu/phase_variation.hpp"
namespace {
using namespace gagp; using namespace gagp::evo; using namespace gagp::evo::repro;
void require(bool b, const std::string& why) { if (!b) throw std::runtime_error(why); }
void check(cudaError_t e) { if (e != cudaSuccess) throw std::runtime_error(cudaGetErrorString(e)); }
template<class T> struct Device {
 T* data = nullptr; std::size_t count;
 explicit Device(std::size_t n):count(n) { check(cudaMalloc(reinterpret_cast<void**>(&data),sizeof(T)*std::max<std::size_t>(1,n))); }
 ~Device() { cudaFree(data); } Device(const Device&) = delete;
 void upload(const std::vector<T>& v) { require(v.size()==count,"upload capacity"); if(count) check(cudaMemcpy(data,v.data(),sizeof(T)*count,cudaMemcpyHostToDevice)); }
 std::vector<T> read() const { std::vector<T> v(count); if(count) check(cudaMemcpy(v.data(),data,sizeof(T)*count,cudaMemcpyDeviceToHost)); return v; }
};
constexpr int population = 1024, capacity = 128;
struct Trees {
 Device<PhaseTreeNode> nodes{population*capacity}; Device<int> origins{population*capacity};
 Device<PhaseGeneSite> sites{population*capacity}; Device<Value> values{population*capacity};
 Device<PhaseGenerationResult> results{population};
};
struct TreePointers {
 PhaseTreeNode* nodes; int* origins; PhaseGeneSite* sites; Value* values; PhaseGenerationResult* results;
 __device__ PhaseTreeView view(int i) const { return {nodes+i*capacity,origins+i*capacity,sites+i*capacity,values+i*capacity,results[i].nodes,results[i].constants}; }
 __device__ MutablePhaseTree output(int i) const { return {nodes+i*capacity,origins+i*capacity,sites+i*capacity,values+i*capacity,capacity}; }
};
TreePointers pointers(Trees& t) { return {t.nodes.data,t.origins.data,t.sites.data,t.values.data,t.results.data}; }
__global__ void initialize(PhaseGrammarView g,int root,int scope,int depth,TreePointers out) {
 const int i=blockIdx.x*blockDim.x+threadIdx.x; if(i>=population)return;
 auto o=out.output(i); auto r=generate_phase(g,root,scope,depth,capacity,UINT64_C(193811)+i,o.nodes,o.origins,o.constants);
 if(r.status==PhaseCompileStatus::Ok && !annotate_phase_tree(g,root,scope,o.nodes,o.origins,r.nodes,o.sites))r.status=PhaseCompileStatus::Invalid;
 out.results[i]=r;
}
__device__ int choose_site(const PhaseTreeView& t,DGrammarRandom& random,int nt=-1,int scope=-1) {
 int chosen=-1,count=0;
 for(int i=0;i<t.node_count;++i)if(t.sites[i].nonterminal>=0 && (nt<0 || (t.sites[i].nonterminal==nt && t.sites[i].scope==scope))) {
  if(random.bounded(++count)==0)chosen=i;
 }
 return chosen;
}
// Operator test only: random parents, no synthetic fitness/search speedup claim.
// Crossover and fresh subtree mutation run entirely on device, with no AST
// decode or host grammar reconstruction between the two passes.
__global__ void cross(PhaseGrammarView g,int root,int scope,int depth,int generation,TreePointers in,TreePointers out,int* changed) {
 const int i=blockIdx.x*blockDim.x+threadIdx.x; if(i>=population)return;
 DGrammarRandom random(UINT64_C(0xc81297)+i+generation*population);
 const int parent=random.bounded(population),donor=random.bounded(population);
 const auto a=in.view(parent),b=in.view(donor); auto output=out.output(i);
 if(in.results[parent].status!=PhaseCompileStatus::Ok || in.results[donor].status!=PhaseCompileStatus::Ok) {out.results[i]={};return;}
 const int destination=choose_site(a,random); const int source=destination<0 ? -1 : choose_site(b,random,a.sites[destination].nonterminal,a.sites[destination].scope);
 auto r=splice_phase_tree(a,destination,b,source,output,depth);
 changed[i]=r.status==PhaseCompileStatus::Ok;
 if(r.status!=PhaseCompileStatus::Ok)r=splice_phase_tree(a,0,a,0,output,depth);
 out.results[i]=r;
}
__global__ void make_mutants(PhaseGrammarView g,int depth,int generation,TreePointers in,TreePointers donors,int* destinations) {
 const int i=blockIdx.x*blockDim.x+threadIdx.x; if(i>=population)return;
 DGrammarRandom random(UINT64_C(0xfeed1812)+generation*population+i); const auto a=in.view(i);
 const int destination=choose_site(a,random);destinations[i]=destination;
 if(destination<0 || in.results[i].status!=PhaseCompileStatus::Ok){donors.results[i]={};return;}
 const auto site=a.sites[destination]; auto o=donors.output(i);
 const int end=phase_subtree_end(a,destination),at_depth=phase_node_depth(a,destination);
 if(end<0 || at_depth<0){donors.results[i]={};return;}
 const int allowed_nodes=capacity-(a.node_count-(end-destination));
 const int allowed_depth=depth-at_depth+1;
 auto r=generate_phase(g,site.entry_expression,site.parent_scope,allowed_depth,allowed_nodes,random.next(),o.nodes,o.origins,o.constants);
 if(r.status==PhaseCompileStatus::Ok && !annotate_phase_tree(g,site.entry_expression,site.parent_scope,o.nodes,o.origins,r.nodes,o.sites))r.status=PhaseCompileStatus::Invalid;
 donors.results[i]=r;
}
__global__ void mutate(PhaseGrammarView g,int root,int scope,int depth,TreePointers in,TreePointers donors,TreePointers out,const int* destinations,int* changed) {
 const int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=population)return;
 const auto a=in.view(i),b=donors.view(i);auto o=out.output(i);
 auto r=donors.results[i].status==PhaseCompileStatus::Ok ? splice_phase_tree(a,destinations[i],b,0,o,depth) : PhaseGenerationResult{};
 changed[i]=r.status==PhaseCompileStatus::Ok;
 if(r.status!=PhaseCompileStatus::Ok)r=splice_phase_tree(a,0,a,0,o,depth);
 out.results[i]=r;
}
constexpr int code_capacity=capacity*4;
__global__ void lower(TreePointers trees,const ValueTag* locals,int local_count,ValueTag expected,
    gpu_detail::DInstr* code,Value* constants,PhaseCompileResult* results) {
 const int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=population)return;
 const auto t=trees.view(i);
 results[i]=compile_phase_tree(t.nodes,t.node_count,t.constants,t.constant_count,locals,local_count,expected,
     code+i*code_capacity,code_capacity,constants+i*capacity,capacity);
}
__global__ void audit_metadata(PhaseGrammarView g,int root,int scope,TreePointers trees,PhaseGeneSite* scratch,int* valid) {
 const int i=blockIdx.x*blockDim.x+threadIdx.x;if(i>=population)return;
 const auto t=trees.view(i);auto* expected=scratch+i*capacity;
 bool ok=trees.results[i].status==PhaseCompileStatus::Ok && annotate_phase_tree(g,root,scope,t.nodes,t.origins,t.node_count,expected);
 if(ok)for(int j=0;j<t.node_count;++j) {
  const auto a=t.sites[j],b=expected[j];
  ok &= a.nonterminal==b.nonterminal && a.entry_expression==b.entry_expression && a.parent_scope==b.parent_scope && a.scope==b.scope;
 }
 valid[i]=ok;
}
__global__ void negative_guards(TreePointers trees,TreePointers output,int* checks) {
 if(threadIdx.x || blockIdx.x)return;
 const auto a=trees.view(0);auto b=a;const auto o=output.output(0);
 int passed=0;
 passed+=splice_phase_tree(a,-1,b,0,o,32).status==PhaseCompileStatus::Invalid;
 passed+=splice_phase_tree(a,a.node_count,b,0,o,32).status==PhaseCompileStatus::Invalid;
 auto bad=b.sites[0];bad.nonterminal+=10000;b.sites=&bad;
 passed+=splice_phase_tree(a,0,b,0,o,32).status==PhaseCompileStatus::Invalid;
 bad=a.sites[0];++bad.scope;
 passed+=splice_phase_tree(a,0,b,0,o,32).status==PhaseCompileStatus::Invalid;
 auto no_room=o;no_room.capacity=0;
 passed+=splice_phase_tree(a,0,a,0,no_room,32).status==PhaseCompileStatus::Invalid;
 passed+=splice_phase_tree(a,0,a,0,o,0).status==PhaseCompileStatus::Invalid;
 // Invalid node lengths must reject before dereferencing a claimed long view.
 b=a;b.node_count=1025;
 passed+=splice_phase_tree(a,0,b,0,o,32).status==PhaseCompileStatus::Invalid;
 checks[0]=passed;
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

std::size_t audit(const grammar::CompiledGrammar& grammar,unsigned root,const HostPhaseGrammar& host,Trees& trees,std::set<std::string>& unique,bool check_lowering=false) {
 std::set<std::string> current;
 const auto nodes=trees.nodes.read();const auto origins=trees.origins.read();const auto values=trees.values.read();const auto results=trees.results.read();
 const auto& nt=grammar.nonterminals().at(root);
 std::vector<gpu_detail::DInstr> compiled_code;std::vector<Value> compiled_constants;std::vector<PhaseCompileResult> lowered;
 if(check_lowering) {
  const auto tag=[](RType type){return type==RType::Int?ValueTag::Int:type==RType::Bool?ValueTag::Bool:ValueTag::IntList;};
  std::vector<ValueTag> types;for(const auto& slot:nt.scope)types.push_back(tag(slot.type));
  Device<ValueTag> locals(types.size());locals.upload(types);
  Device<gpu_detail::DInstr> code(population*code_capacity);Device<Value> constants(population*capacity);Device<PhaseCompileResult> result(population);
  lower<<<(population+63)/64,64>>>(pointers(trees),locals.data,types.size(),tag(nt.type),code.data,constants.data,result.data);check(cudaGetLastError());
  compiled_code=code.read();compiled_constants=constants.read();lowered=result.read();
 }
 grammar::GenerationRequest request{root,nt.type,nt.scope,grammar.search_limits(),grammar::GenerationStage::Mutation};
 grammar::GenerationFrame frame;for(std::size_t i=0;i<nt.scope.size();++i)frame.binder_ids.push_back(100+int(i));
 for(int i=0;i<population;++i) {
  const auto r=results[i];require(r.status==PhaseCompileStatus::Ok,"device variation produced invalid output");
  require(r.nodes>0 && r.nodes<=capacity && r.constants>=0 && r.constants<=r.nodes,"tree capacity");
  ProgramGenome genome;auto& ast=genome.ast;ast.nodes={{NodeKind::PROGRAM},{NodeKind::BLOCK_CONS},{NodeKind::RETURN}};
  ast.consts.assign(values.begin()+i*capacity,values.begin()+i*capacity+r.constants);
  for(int j=0;j<r.nodes;++j) {
   const auto id=origins[i*capacity+j];const auto& e=grammar.expressions().at(id);const auto& model=host.expressions.at(id);const auto node=nodes[i*capacity+j];
   AstNode native{static_cast<NodeKind>(model.native_kind)};
   if(e.kind==grammar::ExpressionKind::Constant)native.i0=node.operand;
   else if(e.kind==grammar::ExpressionKind::Bound)native.i0=frame.binder_ids.at(node.operand);
   if(!e.fuel_charges.empty())ast.fuel_specs.push_back({ast.nodes.size(),e.fuel_charges});
   ast.nodes.push_back(native);
  }
  ast.nodes.push_back({NodeKind::BLOCK_NIL});
  grammar::require_membership_in_frame(grammar,genome,request,frame);
  if(check_lowering) {
   auto projected=grammar::project_frame(grammar,request,frame,ast);ProgramGenome native;native.ast=std::move(projected.ast);
   std::vector<std::string> names;
   // Pure phase profiles reference only lexical inputs, never ordinary inputs.
   for(std::size_t j=projected.inputs.size()-nt.scope.size();j<projected.inputs.size();++j)names.push_back(projected.inputs[j].name);
   const auto verified=verify_ast(native.ast,projected.inputs);require(verified.ok,"independent CPU verification");
   const auto reference=compile_for_eval(native,verified.verified,names);const auto r=lowered[i];
   require(r.status==PhaseCompileStatus::Ok && r.code_count+1==int(reference.code.size()) &&
           r.constant_count==int(reference.consts.size()),"GPU lowering shape differs from independent CPU lowering");
   require(reference.code.back().op==Opcode::Return,"CPU envelope return missing");
   for(int j=0;j<r.code_count;++j) {
    const auto x=compiled_code[i*code_capacity+j];const auto y=reference.code[j];
    require(x.op==int(y.op) && x.flags==unsigned((y.has_a?1:0)|(y.has_b?2:0)) && x.a==y.a && x.b==y.b &&
       x.fuel==(reference.instruction_fuel.empty()?1:reference.instruction_fuel[j]),"GPU instruction/fuel differs from CPU");
   }
   for(int j=0;j<r.constant_count;++j) {
    const auto x=compiled_constants[i*capacity+j],y=reference.consts[j];
    require(x.tag==y.tag && (x.tag==ValueTag::Bool?x.b==y.b:x.i==y.i),"GPU constant differs from CPU");
   }
  }
  unique.insert(ast_cache_key(ast));current.insert(ast_cache_key(ast));
 }
 return current.size();
}
void run(const grammar::CompiledGrammar& grammar,unsigned root,const HostPhaseGrammar& host,bool expect_diversity) {
 Device<PhaseExpression> e(host.expressions.size());Device<PhaseProduction> p(host.productions.size());
 Device<PhaseNonterminal> n(host.nonterminals.size());Device<PhaseDomain>d(host.domains.size());Device<Value>v(host.values.size());Device<unsigned>m(host.minimum.size());
 e.upload(host.expressions);p.upload(host.productions);n.upload(host.nonterminals);d.upload(host.domains);v.upload(host.values);m.upload(host.minimum);
 PhaseGrammarView g{e.data,int(e.count),p.data,int(p.count),n.data,int(n.count),d.data,int(d.count),v.data,int(v.count),m.data,host.depth_count};
 Trees a,b,donors;Device<int> destinations(population),changed(population),cross_changed(population),metadata_valid(population);
 Device<PhaseGeneSite> audit_sites(population*capacity);
 const auto tag=[](RType t){return t==RType::Int?ValueTag::Int:t==RType::Bool?ValueTag::Bool:ValueTag::IntList;};
 std::vector<ValueTag> types;for(const auto& slot:grammar.nonterminals()[root].scope)types.push_back(tag(slot.type));
 Device<ValueTag> locals(types.size());locals.upload(types);
 Device<gpu_detail::DInstr> code(population*code_capacity);Device<Value> constants(population*capacity);Device<PhaseCompileResult> lowered(population);
 cudaEvent_t start,stop;check(cudaEventCreate(&start));check(cudaEventCreate(&stop));std::vector<float> timings;
 const int scope=grammar.nonterminals()[root].scope.size();const int depth=std::min(10,host.depth_count-1);
 initialize<<<(population+63)/64,64>>>(g,host.root_expression,scope,depth,pointers(a));check(cudaGetLastError());check(cudaDeviceSynchronize());
 negative_guards<<<1,1>>>(pointers(a),pointers(b),changed.data);check(cudaGetLastError());
 require(changed.read()[0]==7,"negative safety/contract guard failed");
 std::set<std::string> unique;auto final_unique=audit(grammar,root,host,a,unique,true);unsigned long long crossed=0,mutated=0;
 for(int generation=0;generation<32;++generation) {
  check(cudaEventRecord(start));
  cross<<<(population+63)/64,64>>>(g,host.root_expression,scope,depth,generation,pointers(a),pointers(b),cross_changed.data);check(cudaGetLastError());
  make_mutants<<<(population+63)/64,64>>>(g,depth,generation,pointers(b),pointers(donors),destinations.data);check(cudaGetLastError());
  mutate<<<(population+63)/64,64>>>(g,host.root_expression,scope,depth,pointers(b),pointers(donors),pointers(a),destinations.data,changed.data);check(cudaGetLastError());
  lower<<<(population+63)/64,64>>>(pointers(a),locals.data,types.size(),tag(grammar.nonterminals()[root].type),code.data,constants.data,lowered.data);check(cudaGetLastError());
  check(cudaEventRecord(stop));check(cudaEventSynchronize(stop));float ms;check(cudaEventElapsedTime(&ms,start,stop));timings.push_back(ms);
  for(auto x:changed.read())mutated+=x;for(auto x:cross_changed.read())crossed+=x;
  for(const auto& r:lowered.read())require(r.status==PhaseCompileStatus::Ok,"new GPU genotype did not lower");
  audit_metadata<<<(population+63)/64,64>>>(g,host.root_expression,scope,pointers(a),audit_sites.data,metadata_valid.data);check(cudaGetLastError());
  for(auto valid:metadata_valid.read())require(valid,"transported GPU site metadata differs from independent reconstruction");
  if(generation%8==7)final_unique=audit(grammar,root,host,a,unique,generation==31);
 }
 cudaEventDestroy(start);cudaEventDestroy(stop);
 require(crossed>0 && mutated==population*32 && !unique.empty(),"variation failed to construct valid trees");
 // The built-in recursive grammar has a large language. External fixtures can
 // legitimately have a finite language (e.g. two bound variables only).
 if(expect_diversity)require(unique.size()>population,"recursive fixture lost diverse offspring");
 std::cout<<"root="<<grammar.nonterminals()[root].stable_id<<" audited="<<population*5<<" unique="<<unique.size()<<" unique_final="<<final_unique<<" cross_accepted="<<crossed<<" mutation_accepted="<<mutated<<" attempted="<<population*32<<" operator_lower_ms=[";
 for(std::size_t i=0;i<timings.size();++i)std::cout<<(i?",":"")<<timings[i];std::cout<<"]"<<std::endl;
}
}
int main(int argc,char** argv) {try {
 require(argc<=3,"usage: gagp_test_gpu_phase_variation [GRAMMAR_JSON [EXPECTED_ROOTS]]");
 const auto grammar=grammar::compile_grammar(argc>1?grammar::load_definition(argv[1]):grammar::parse_definition(scalar_definition()));
 FitnessSessionGpu session;require(session.init({{}},{Value::from_int(0)},1,1,1).ok,"CUDA initialization");
 int supported=0;
 for(const auto& nt:grammar.nonterminals()) {
  HostPhaseGrammar profile;
  try {profile=compile_phase_variation_grammar(grammar,nt.id,std::min<unsigned>(32,grammar.search_limits().max_depth));}
  catch(const std::invalid_argument& error){std::cout<<"unsupported "<<nt.stable_id<<": "<<error.what()<<std::endl;continue;}
  run(grammar,nt.id,profile,argc==1);++supported;
 }
 if(argc==3)require(supported==std::stoi(argv[2]),"profile support changed");
 else require(supported>0,"no supported grammar root");
 return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
