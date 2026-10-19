// Isolated fixed-parent diagnosis. This executable is never part of search.
#include <cuda_runtime.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "adapter.hpp"
#include "lang/expr.h"
#include "gagp/evolution/node_descriptor.hpp"
#include "gagp/evolution/grammar/identity.hpp"
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "phase_layout.hpp"

namespace {
using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::repro;
using namespace fixed_asgp;
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void checked(cudaError_t status) {if(status!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(status));}
Json boolean(bool value) {Json out;out.kind=Json::Kind::Bool;out.bool_v=value;return out;}
template<class T> struct Device {
 T* data=nullptr;std::size_t count;
 explicit Device(std::size_t n):count(n) {checked(cudaMalloc(reinterpret_cast<void**>(&data),sizeof(T)*std::max<std::size_t>(1,n)));}
 explicit Device(const std::vector<T>& values):Device(values.size()) {if(count)checked(cudaMemcpy(data,values.data(),sizeof(T)*count,cudaMemcpyHostToDevice));}
 ~Device(){cudaFree(data);}Device(const Device&)=delete;
 std::vector<T> read()const {std::vector<T> out(count);if(count)checked(cudaMemcpy(out.data(),data,sizeof(T)*count,cudaMemcpyDeviceToHost));return out;}
};
// Normalize serialization tables only. Preserve all node, scope, region and fuel
// data; different constant pool interning orders cannot be operator differences.
std::string key(ProgramGenome genome) {
 std::vector<Value> constants;
 for(auto& node:genome.ast.nodes) {
  const auto& descriptor=node_descriptor(node.kind);
  for(auto field:{std::make_pair(descriptor.i0_role,&node.i0),std::make_pair(descriptor.i1_role,&node.i1)}) {
   if(field.first!=NodeIndexRole::Constant)continue;
   const auto v=genome.ast.consts.at(*field.second);
   require(v.tag==ValueTag::Int || v.tag==ValueTag::Bool ||
       (v.tag==ValueTag::IntList && Value::container_len(v)==0),"unsupported constant normalization");
   auto found=std::find_if(constants.begin(),constants.end(),[&](Value u){
    return u.tag==v.tag && (v.tag==ValueTag::Int?u.i==v.i:v.tag==ValueTag::Bool?u.b==v.b:true);
   });
   *field.second=found-constants.begin();if(found==constants.end())constants.push_back(v);
  }
 }
 genome.ast.consts=std::move(constants);
 std::stable_sort(genome.ast.fuel_specs.begin(),genome.ast.fuel_specs.end(),[](const auto& a,const auto& b){return a.node_index<b.node_index;});
 return ast_cache_key(genome.ast);
}
struct SpliceReply {PhaseGenerationResult result;int annotation_base=0,annotation_donor=0,negative_guards=0;};
__global__ void replace_root(PhaseGrammarView grammar,int root,int scope,int depth,
 PhaseTreeView base,PhaseTreeView donor,PhaseGeneSite* base_sites,PhaseGeneSite* donor_sites,
 MutablePhaseTree output,SpliceReply* reply) {
 if(blockIdx.x || threadIdx.x)return;
 SpliceReply out;
 out.annotation_base=annotate_phase_tree(grammar,root,scope,base.nodes,base.origins,base.node_count,base_sites);
 out.annotation_donor=annotate_phase_tree(grammar,root,scope,donor.nodes,donor.origins,donor.node_count,donor_sites);
 base.sites=base_sites;donor.sites=donor_sites;
 if(out.annotation_base && out.annotation_donor) {
  out.result=splice_phase_tree<true>(base,0,donor,0,output,depth);
  // Failed output bytes are unspecified and never consumed. Both guards reject
  // before writing any byte of the successful output we retain above.
  out.negative_guards+=splice_phase_tree<true>(base,-1,donor,0,output,depth).status==PhaseCompileStatus::Invalid;
  out.negative_guards+=splice_phase_tree<true>(base,0,donor,0,output,0).status==PhaseCompileStatus::Invalid;
 }
 *reply=out;
}
HostPhaseGene gpu_replace(const HostPhaseGrammar& profile,const HostPhaseGene& base,
 const HostPhaseGene& donor,int scope,int depth,SpliceReply& reply) {
 Device<PhaseExpression> expressions(profile.expressions);Device<PhaseProduction> productions(profile.productions);
 Device<PhaseNonterminal> nonterminals(profile.nonterminals);Device<PhaseDomain> domains(profile.domains);
 Device<Value> values(profile.values);Device<unsigned> minimum(profile.minimum);
 PhaseGrammarView grammar{expressions.data,int(expressions.count),productions.data,int(productions.count),
  nonterminals.data,int(nonterminals.count),domains.data,int(domains.count),values.data,int(values.count),
  minimum.data,profile.depth_count,profile.logical_depth_count,profile.site_depth};
 Device<PhaseTreeNode> an(base.nodes),bn(donor.nodes),nodes(1024);
 Device<int> ao(base.origins),bo(donor.origins),origins(1024);
 Device<Value> ac(base.constants),bc(donor.constants),constants(1024);
 Device<PhaseGeneSite> as(base.nodes.size()),bs(donor.nodes.size()),sites(1024);Device<SpliceReply> result(1);
 PhaseTreeView a{an.data,ao.data,nullptr,ac.data,int(an.count),int(ac.count)},b{bn.data,bo.data,nullptr,bc.data,int(bn.count),int(bc.count)};
 MutablePhaseTree output{nodes.data,origins.data,sites.data,constants.data,1024};
 replace_root<<<1,1>>>(grammar,profile.root_expression,scope,depth,a,b,as.data,bs.data,output,result.data);
 checked(cudaGetLastError());checked(cudaDeviceSynchronize());reply=result.read()[0];
 HostPhaseGene out;
 if(reply.result.status==PhaseCompileStatus::Ok) {
  require(reply.result.nodes>0 && reply.result.nodes<=1024 && reply.result.constants>=0 && reply.result.constants<=1024,"invalid GPU publication lengths");
  out.nodes=nodes.read();out.nodes.resize(reply.result.nodes);out.origins=origins.read();out.origins.resize(reply.result.nodes);
  out.constants=constants.read();out.constants.resize(reply.result.constants);
 }
 return out;
}
Json diagnose(const Json& source,const std::string& grammar_path) {
 const auto name=source.object_v.at("task").string_v;
 const auto definition=gg::load_definition(grammar_path);const auto grammar=gg::compile_grammar(definition);
 const auto& population=source.object_v.at("population").array_v;require(population.size()>=8,"requires fixed first8 source parents");
 std::vector<Json> rows;
 for(int pair=0;pair<4;++pair) {
  const int first=2*pair,second=first+1;
  const auto base=decode(population.at(first)),donor=decode(population.at(second));
  const int phases=population.at(first).array_v.size();
  for(int phase=0;phase<phases;++phase) {
   Json row=object({{"task",string(name)},{"pair",number(pair)},{"parent",number(first)},{"donor",number(second)},
    {"source_phase",number(phase)},{"source_destination",number(0)},{"source_donor_site",number(0)},
    {"operation",string("explicit_full_phase_crossover")},{"status",string("not_run")}});
   std::string failure_stage="source_operation";
   try {
    auto child=base;require(!child.phases[phase].empty() && !donor.phases[phase].empty(),"empty source phase");
    asgp::ReplaceSubtree(child.phases[phase],0,donor.phases[phase].data(),donor.phases[phase].size());
    row.object_v["source_child"]=encode(child,phases);
    row.object_v["source_depth"]=number(asgp::SubtreeDepth(child.phases[phase]));
    failure_stage="native_translation";
    const auto a=translate(base,name,definition,grammar),b=translate(donor,name,definition,grammar);
    const auto expected=translate(child,name,definition,grammar);const auto expected_key=key(expected);
    row.object_v["expected_child_sha256"]=string(gg::content_sha256(expected_key));
    failure_stage="native_layout";
    const auto la=import_native_phase_layout(grammar,a),lb=import_native_phase_layout(grammar,b);
    std::vector<std::pair<int,int>> mapping;
    for(int x=0;x<int(la.slots.size());++x)for(int y=0;y<int(lb.slots.size());++y) {
     if(la.slots[x].nonterminal!=lb.slots[y].nonterminal || la.slots[x].ordinal!=lb.slots[y].ordinal)continue;
     auto genes=la.genes;genes[x]=lb.genes[y];
     try {if(key(export_native_phase_layout(grammar,la,genes))==expected_key)mapping.emplace_back(x,y);}
     catch(const std::invalid_argument&) {}
    }
    row.object_v["mapping_candidates"]=number(mapping.size());
    if(mapping.size()!=1) {
     row.object_v["status"]=string(mapping.empty()?"mapping_unavailable":"mapping_ambiguous");
    } else {
     const auto [x,y]=mapping.front();row.object_v["native_slot"]=number(x);row.object_v["native_donor_slot"]=number(y);
     failure_stage="gpu_splice";
     SpliceReply reply;auto actual_gene=gpu_replace(la.profiles[x],la.genes[x],lb.genes[y],la.slots[x].binders.size(),la.slots[x].max_depth,reply);
     row.object_v["gpu_status"]=number(int(reply.result.status));
     row.object_v["annotation_base"]=number(reply.annotation_base);row.object_v["annotation_donor"]=number(reply.annotation_donor);
     row.object_v["negative_guards"]=number(reply.negative_guards);
     if(reply.result.status!=PhaseCompileStatus::Ok)row.object_v["status"]=string("gpu_rejected");
     else {
      failure_stage="gpu_child_admission";
      auto genes=la.genes;genes[x]=std::move(actual_gene);const auto actual=export_native_phase_layout(grammar,la,genes);
      gg::require_membership(grammar,actual);const auto actual_key=key(actual);
      row.object_v["actual_child_sha256"]=string(gg::content_sha256(actual_key));
      row.object_v["child_equal"]=boolean(actual_key==expected_key);
      row.object_v["status"]=string(actual_key==expected_key && reply.negative_guards==2?"ok":"mismatch");
     }
    }
   } catch(const std::exception& error) {
    row.object_v["status"]=string(failure_stage=="gpu_splice"?"gpu_failed":failure_stage=="gpu_child_admission"?"native_child_unadmitted":failure_stage=="source_operation"?"source_invalid":"native_unadmitted");
    row.object_v["failure_stage"]=string(failure_stage);row.object_v["reason"]=string(error.what());
   }
   rows.push_back(std::move(row));
  }
 }
 return object({{"format",string("fixed-parent-root-splice-diagnosis-v1")},{"task",string(name)},
  {"scope",string("first8 fixed source parents;4 pairs; explicit full source-phase roots only; mapping failures preserved; no internal-site/mutation-generator/RNG distribution or fitness-equivalence claim")},
  {"rows",array(std::move(rows))}});
}
}
int main(int argc,char** argv) {
 try {
  require(argc==4,"usage: operator_diagnosis SOURCE GRAMMAR NEW_OUTPUT");
  require(!std::filesystem::exists(argv[3]),"output exists; preserve earlier attempt");
  gagp::FitnessSessionGpu session;require(session.init({{}},{gagp::Value::from_int(0)},1,1,1).ok,"GPU initialization failed");
  fixed_asgp::write(argv[3],diagnose(fixed_asgp::read(argv[1]),argv[2]));
  return 0;
 } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
