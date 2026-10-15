// Run the shared bounded construction/variation algorithms on host memory.
// These routines contain no device intrinsics; CUDA execution is tested separately.
#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdexcept>
#ifndef __CUDACC__
#define __device__
#define __host__
#endif
using std::max;
#include "../../src/evolution/repro/gpu/phase_layout.hpp"
#include "gagp/evolution/grammar/definition.hpp"
using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::repro;
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main()try {
 const auto grammar=grammar::compile_grammar(grammar::parse_definition(R"({
 "format_version":"grammar-definition-v2","entry":{"nonterminal":"E","type":"Int"},
 "search_limits":{"max_nodes":64,"max_depth":12},"execution_limits":{"fuel":1000},
 "nonterminals":[{"id":"E","type":"Int","scope":[],"alternatives":[
 {"id":"zero","weight":1,"expression":{"constant":{"type":"Int","values":["0"]}}},
 {"id":"expanded-terminal","weight":1,"expression":{"signature":"add(Int,Int)->Int","args":[
 {"constant":{"type":"Int","values":["1"]},"resource_charge":{"nodes":1,"depth":0,"resets_depth":false}},
 {"constant":{"type":"Int","values":["2"]},"resource_charge":{"nodes":1,"depth":0,"resets_depth":false}}]}},
 {"id":"recursive","weight":1,"expression":{"signature":"neg(Int)->Int","args":[{"ref":"E"}]}}
 ]}]})"));
 require(grammar.resource_invariant_roots({grammar.entry()}).front(),"fixture certificate");
 const auto host=compile_phase_variation_grammar(grammar,grammar.entry(),8,2);
 PhaseGrammarView g{host.expressions.data(),int(host.expressions.size()),host.productions.data(),int(host.productions.size()),
 host.nonterminals.data(),int(host.nonterminals.size()),host.domains.data(),int(host.domains.size()),
 host.values.data(),int(host.values.size()),host.minimum.data(),host.depth_count,host.logical_depth_count};
 struct Tree {
  PhaseTreeNode nodes[64];int origins[64];Value values[64];PhaseGeneSite sites[64];PhaseGenerationResult result;
  PhaseTreeView view()const{return {nodes,origins,sites,values,result.nodes,result.constants};}
  MutablePhaseTree out(){return {nodes,origins,sites,values,64};}
 } a,b,out;
 bool expanded=false,deep=false,rejected=false,accepted=false;
 for(unsigned seed=0;seed<512;++seed) {
  a.result=generate_phase<true,true>(g,host.root_expression,0,8,64,seed,a.nodes,a.origins,a.values,a.sites,2);
  b.result=generate_phase<false,true>(g,host.root_expression,0,8,64,seed,b.nodes,b.origins,b.values,b.sites,2);
  require(a.result.status==PhaseCompileStatus::Ok && b.result.status==PhaseCompileStatus::Ok,"projected generation failed");
  require(a.result.nodes==b.result.nodes,"compact/reference task result diverged");
  for(int i=0;i<a.result.nodes;++i)require(a.origins[i]==b.origins[i],"compact/reference origin diverged");
  const int height=phase_projected_depth(g,a.nodes,a.origins,a.result.nodes);
  require(height>=1 && height<=2,"logical budget exceeded");
  expanded|=phase_tree_depth(a.nodes,a.result.nodes)>height;
  deep|=height==2;
  for(int i=0;i<a.result.nodes;++i)if(a.sites[i].nonterminal>=0) {
   auto r=splice_phase_tree(a.view(),i,b.view(),0,out.out(),8,&g,2);
   if(r.status==PhaseCompileStatus::Ok) {
    accepted=true;require(phase_projected_depth(g,out.nodes,out.origins,r.nodes)<=2,"splice exceeded logical budget");
   } else {require(r.status==PhaseCompileStatus::Capacity,"unexpected splice failure");rejected=true;}
  }
 }
 require(expanded && deep && rejected && accepted,"missing projected test cases");
 // An expanded terminal fits logical depth one but still needs physical depth two.
 for(unsigned seed=0;seed<64;++seed) {
  a.result=generate_phase<true,true>(g,host.root_expression,0,1,64,seed,a.nodes,a.origins,a.values,a.sites,1);
  require(a.result.status==PhaseCompileStatus::Ok && a.result.nodes==1,"physical constraint bypassed");
 }
 std::cout<<"projected phase generation/splice host checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
