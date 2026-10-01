#include "../../src/evolution/grammar/executable_fragments.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/core/bytecode_verify.hpp"
#include <cstdlib>
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/serialization/region_plan_json.hpp"
#include "../fixtures/bounded_capture.hpp"
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <set>
#include <type_traits>

// A caller must not create a mutable certificate copy and replace its contents
// after composition. Sharing const handles is the only supported ownership.
static_assert(!std::is_copy_constructible_v<gagp::RegionExecutableLayout>);
static_assert(!std::is_copy_assignable_v<gagp::RegionExecutableLayout>);
static_assert(!std::is_move_constructible_v<gagp::RegionExecutableLayout>);
static_assert(!std::is_move_assignable_v<gagp::RegionExecutableLayout>);
static_assert(!std::is_copy_constructible_v<gagp::RegionExecutablePhase>);
static_assert(!std::is_copy_assignable_v<gagp::RegionExecutablePhase>);
static_assert(!std::is_move_constructible_v<gagp::RegionExecutablePhase>);
static_assert(!std::is_move_assignable_v<gagp::RegionExecutablePhase>);

using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;
void require(bool x, const char* s) {if(!x)throw std::runtime_error(s);}
template<class F> void rejects(F f) {bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}require(rejected,"expected explicit rejection");}
std::shared_ptr<const CompiledGrammar> fixture() {
  RegionPlan plan;plan.state_types={ValueTag::Int};plan.parameter_types={ValueTag::Int};plan.result_type=ValueTag::Int;
  plan.coordinate_slots={0};plan.coordinate_rank={{0,1}};
  plan.coordinate_domains={{{RegionBoundKind::Literal,0,0},{RegionBoundKind::Literal,5,0}}};
  RegionStateTransition edge;edge.kind=RegionTransitionKind::CoordinateOffset;edge.offset=-1;
  plan.requests={{{edge}}};plan.memoized=true;plan.limits={8,8,1};
  std::string text=R"JSON({"format_version":"grammar-definition-v2",
    "entry":{"nonterminal":"Main","type":"Int"},"inputs":[{"name":"seed","type":"Int"}],
    "search_limits":{"max_nodes":100,"max_depth":20},"execution_limits":{"fuel":1000},
    "templates":[
      {"id":"Region","type":"Int","scope":[],"holes":[
        {"id":"base","type":"Int","scope":[{"name":"seed","type":"Int"}]},
        {"id":"combine","type":"Int","scope":[{"name":"child","type":"Int"}]}],
       "body":{"structured":{"family":"bounded","plan":PLAN},"captures":[{"input":"seed"}],
       "phases":[{"argument":1,"bindings":[{"bank":"state","slot":0,"name":"n"}]},
         {"argument":2,"bindings":[{"bank":"parameter","slot":0,"name":"seed"}]},
         {"argument":3,"bindings":[{"bank":"result","slot":0,"name":"child"}]},
         {"argument":4,"bindings":[]}],
       "args":[{"constant":{"type":"Int","values":["3"]}},
         {"signature":"le(Int,Int)->Bool","args":[{"bound":"n"},{"constant":{"type":"Int","values":["0"]}}]},
         {"hole":"base"},{"hole":"combine"},{"constant":{"type":"Int","values":["0"]}}]}},
      {"id":"Twice","type":"Int","scope":[{"name":"child","type":"Int"}],
       "holes":[{"id":"x","type":"Int","scope":[{"name":"child","type":"Int"}]}],
       "body":{"signature":"add(Int,Int)->Int","args":[{"hole":"x"},{"hole":"x"}]}}],
    "nonterminals":[
      {"id":"Main","type":"Int","scope":[],"alternatives":[{"id":"main","weight":1,
       "expression":{"template":"Region","holes":{"base":{"ref":"Base"},"combine":{"ref":"Combine"}}}}]},
      {"id":"Base","type":"Int","scope":[{"name":"seed","type":"Int"}],"alternatives":[
       {"id":"seed","weight":1,"expression":{"bound":"seed"}},
       {"id":"constant","weight":1,"expression":{"constant":{"type":"Int","values":["3","5"]}}}]},
      {"id":"Combine","type":"Int","scope":[{"name":"child","type":"Int"}],"alternatives":[
       {"id":"twice","weight":1,"expression":{"template":"Twice","holes":{"x":{"ref":"Leaf"}}}}]},
      {"id":"Leaf","type":"Int","scope":[{"name":"child","type":"Int"}],"alternatives":[
       {"id":"child","weight":1,"expression":{"bound":"child"}},
       {"id":"constant","weight":1,"expression":{"constant":{"type":"Int","values":["1","2"]}}}]}]
    })JSON";
  text.replace(text.find("PLAN"),4,canonical_json(serialization::encode_region_plan(plan)));
  return std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(text)));
}
int main() {
 try {
  const auto grammar=fixture();auto a=generate_derivation(*grammar,0).genome;
  ExecutableFragments arena(grammar,a,{"seed"}),other(grammar,a,{"seed"});
  // Import must remain bound to the admitted grammar even when the caller
  // replaces a mutable object originally passed through a const shared_ptr.
  auto mutable_grammar=std::make_shared<CompiledGrammar>(*grammar);
  ExecutableFragments frozen(mutable_grammar,a,{"seed"});
  *mutable_grammar=compile_grammar(parse_definition(test::bounded_capture_definition()));
  auto frozen_handle=frozen.import(a);
  require_membership(*grammar,frozen.export_ast(frozen_handle));
  rejects([&]{arena.executable({});});
  rejects([&]{arena.executable(other.import(a));});
  auto reordered=a;
  std::reverse(reordered.ast.consts.begin(),reordered.ast.consts.end());
  for(auto& node:reordered.ast.nodes)if(node.kind==NodeKind::CONST)
    node.i0=static_cast<int>(reordered.ast.consts.size())-1-node.i0;
  (void)arena.import(reordered); // equal content, different constant-pool layout
  auto corrupt=a;for(auto& value:corrupt.ast.consts)if(value.tag==ValueTag::Int)value.i=999;
  rejects([&]{arena.import(corrupt);});
  corrupt=a;for(auto& n:corrupt.ast.nodes)if(n.kind==NodeKind::REGION_VAR)n.i0+=999;
  rejects([&]{arena.import(corrupt);});
  std::vector<ExecutableFragments::Genome> population;
  for(unsigned seed=0;seed<16;++seed)population.push_back(arena.import(generate_derivation(*grammar,seed).genome));
  std::size_t changed=0;std::set<std::string> genotypes;
  for(unsigned generation=0;generation<40;++generation) {
    for(unsigned i=0;i<population.size();++i) {
      auto result=arena.vary(population[i],population[(i+1)%population.size()],generation*100+i,(i+generation)%2==0);
      changed+=result.changed;population[i]=std::move(result.genome);
      auto ast=arena.export_ast(population[i]);
      require_membership(*grammar,ast); // catches a split logical hole independently
      genotypes.insert(ast_cache_key(ast.ast));
      auto from_ast=compile_for_eval(ast,{"seed"});auto from_handle=arena.executable(population[i]);
      if(std::getenv("GAGP_OWNED_EXECUTABLE")) {
        const auto owned=arena.owned_executable(population[i]);
        const auto validated=verify_bytecode(owned.materialize());
        require(validated.ok && owned.stack_bound()>=validated.verified.max_stack_depth,"composed verification bound invalid");
      }
      for(int fuel:{0,1,4,20,1000})for(int input:{2,7}) {
        auto x=execute_bytecode_cpu(from_ast,{{0,Value::from_int(input)}},fuel);
        auto y=execute_bytecode_cpu(from_handle,{{0,Value::from_int(input)}},fuel);
        require(x.is_error==y.is_error && (x.is_error?x.err.code==y.err.code:x.value.tag==y.value.tag && x.value.i==y.value.i),"export executable/fuel mismatch");
      }
    }
    require(arena.live_fragments()<=population.size()*2,"retired sources were retained");
  }
  require(changed>0 && genotypes.size()>2,"variation collapsed");
  auto survivor=population[0];population.clear();require(arena.live_fragments()==2,"last-owner collection failed");
  {auto transient=std::make_unique<ExecutableFragments>(grammar,a,std::vector<std::string>{"seed"});
   survivor=transient->import(a);}
  rejects([&]{arena.executable(survivor);}); // owner lifetime is safe, owner identity remains distinct
  auto unsupported=std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(test::bounded_capture_definition())));
  rejects([&]{ExecutableFragments bad(unsupported,generate_derivation(*unsupported,0).genome,{});});
  std::cout<<"executable fragments: ownership, membership, atomic holes, fuel and 40-generation collection OK\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
