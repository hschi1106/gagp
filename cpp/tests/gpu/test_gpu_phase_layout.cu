#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <optional>
#include "gagp/cli/program_json.hpp"
#include "gagp/evolution/grammar/derivation_resources.hpp"
#include "../../src/evolution/repro/gpu/phase_layout.hpp"
#include "../../src/evolution/region_plan_equal.hpp"
#include "../fixtures/bounded_capture.hpp"
namespace {
using namespace gagp; using namespace gagp::evo; using namespace gagp::evo::repro;
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
template<class P> void same_program(const P& a,const P& b) {
 require(a.code.size()==b.code.size() && a.consts.size()==b.consts.size() &&
     a.n_locals==b.n_locals && a.var2idx==b.var2idx && a.instruction_fuel==b.instruction_fuel,"bytecode shape changed on genotype roundtrip");
 for(std::size_t i=0;i<a.code.size();++i) {
  const auto x=a.code[i],y=b.code[i];require(x.op==y.op && x.a==y.a && x.b==y.b && x.has_a==y.has_a && x.has_b==y.has_b,"instruction changed");
 }
 for(std::size_t i=0;i<a.consts.size();++i) {
  const auto x=a.consts[i],y=b.consts[i];require(x.tag==y.tag && (x.tag==ValueTag::Bool?x.b==y.b:x.i==y.i),"constant changed");
 }
}
void same_phase(const RegionPhase& a,const RegionPhase& b) {
 same_program(a.program,b.program);require(a.program.binder_locals==b.program.binder_locals && a.bindings.size()==b.bindings.size(),"phase scope changed");
 for(std::size_t i=0;i<a.bindings.size();++i)require(a.bindings[i].source.bank==b.bindings[i].source.bank &&
     a.bindings[i].source.slot==b.bindings[i].source.slot && a.bindings[i].local==b.bindings[i].local,"phase binding changed");
}
void same_bytecode(const BytecodeProgram& a,const BytecodeProgram& b) {
 same_program(a,b);require(a.bounded_region_segments.size()==b.bounded_region_segments.size(),"segment count changed");
 for(std::size_t i=0;i<a.bounded_region_segments.size();++i) {
  const auto& x=a.bounded_region_segments[i];const auto& y=b.bounded_region_segments[i];
  require(same_region_plan(x.plan,y.plan) && x.parameter_locals==y.parameter_locals &&
      x.preparations.size()==y.preparations.size() && x.request_expressions.size()==y.request_expressions.size() &&
      x.boundary.has_value()==y.boundary.has_value(),"region contract changed");
  same_phase(x.base_predicate,y.base_predicate);same_phase(x.base_body,y.base_body);same_phase(x.combine,y.combine);
  for(std::size_t j=0;j<x.preparations.size();++j)same_phase(x.preparations[j],y.preparations[j]);
  for(std::size_t j=0;j<x.request_expressions.size();++j)same_phase(x.request_expressions[j],y.request_expressions[j]);
  if(x.boundary)same_phase(*x.boundary,*y.boundary);
 }
}
void check_import(const grammar::CompiledGrammar& grammar,const ProgramGenome& genome,bool expect_scalar_certificate=true) {
 auto fresh=genome;fresh.derivation.reset();
 const auto layout=import_native_phase_layout(grammar,fresh);
 // Constant-pool numbering is incidental, including constants first interned
 // inside a variable phase and reused by a later fixed request expression.
 auto reordered_pool=fresh;
 std::reverse(reordered_pool.ast.consts.begin(),reordered_pool.ast.consts.end());
 for(auto& node:reordered_pool.ast.nodes)if(node.kind==NodeKind::CONST)
  node.i0=static_cast<int>(reordered_pool.ast.consts.size())-1-node.i0;
 const auto reordered_layout=import_native_phase_layout(grammar,reordered_pool);
 const auto key=native_phase_skeleton_key(layout);
 require(key==native_phase_skeleton_key(reordered_layout),"equivalent constant pools changed fixed skeleton");
 auto changed=layout;
 bool tested_fixed=false;
 for(std::size_t i=0;i<changed.prototype.nodes.size();++i) {
  const auto& node=changed.prototype.nodes[i];
  const bool variable=std::any_of(layout.slots.begin(),layout.slots.end(),
      [&](const auto& slot){return slot.begin<=i && i<slot.end;});
  if(node.kind!=NodeKind::CONST || variable)continue;
  changed.prototype.consts[node.i0]=Value::from_int(9007199254740993LL);
  const auto wide_key=native_phase_skeleton_key(changed);
  require(key!=wide_key,"different fixed constant admitted");
  changed.prototype.consts[node.i0]=Value::from_int(9007199254740992LL);
  require(wide_key!=native_phase_skeleton_key(changed),"fixed integer identity rounded");
  changed.prototype.consts[node.i0]=Value::from_int_list_hash_len(123,0);
  const auto payload_key=native_phase_skeleton_key(changed);
  changed.prototype.consts[node.i0]=Value::from_int_list_hash_len(124,0);
  require(payload_key!=native_phase_skeleton_key(changed),"fixed payload identity erased");
  tested_fixed=true;break;
 }
 require(tested_fixed,"fixture needs a fixed constant");
 changed=layout;changed.prototype.bounded_region_specs[0].plan.limits.entry_fuel+=1;
 require(key!=native_phase_skeleton_key(changed),"fixed plan changed without rejection");
 changed=layout;changed.prototype.bounded_region_specs[0].phases[0].bindings[0].binder_id+=1;
 require(key!=native_phase_skeleton_key(changed),"fixed binding changed without rejection");
 changed=layout;changed.prototype.fuel_specs.push_back({0,{{FuelEvent::Operation,123}}});
 require(key!=native_phase_skeleton_key(changed),"fixed fuel changed without rejection");
 const auto restored=export_native_phase_layout(grammar,layout,layout.genes);
 // Independent canonical admission, with no carried provenance/certificate.
 require(!restored.derivation,"export accidentally attached a trusted proof");
 grammar::require_membership(grammar,restored);
 std::vector<std::string> inputs;for(const auto& input:grammar.inputs())inputs.push_back(input.name);
 same_bytecode(layout.executable,compile_for_eval(restored,inputs));
 std::size_t total=layout.fixed_nodes;
 for(std::size_t i=0;i<layout.slots.size();++i) {
  const auto& slot=layout.slots[i];total+=layout.genes[i].nodes.size();
  require(layout.genes[i].nodes.size()==slot.end-slot.begin && slot.max_depth>0,"layout resource span");
 }
 require(total==genome.ast.nodes.size(),"whole program materialized nodes lost");
 // Export buffers are not a public membership certificate.
 auto stale=genome;stale.ast.consts.push_back(Value::invalid());bool rejected=false;
 try{(void)import_native_phase_layout(grammar,stale);}catch(const std::invalid_argument&){rejected=true;}
 require(rejected,"cold import ignored malformed unused constants");
 auto admitted=genome;
 admitted.derivation=std::make_shared<const grammar::DerivationMetadata>(
     grammar::detail::reconstruct_cold_derivation(grammar,admitted,grammar::entry_request(grammar)));
 require(!expect_scalar_certificate || bool(admitted.derivation->certificate),"import reuse fixture needs private admission");
 same_bytecode(layout.executable,import_native_phase_layout(grammar,admitted).executable);
 // A different evaluator order must lower for that order, not reuse the
 // canonical grammar-input executable. The fixture has multiple inputs.
 auto reordered=inputs;std::reverse(reordered.begin(),reordered.end());
 same_bytecode(compile_for_eval(admitted,reordered),
     import_native_phase_layout(grammar,admitted,nullptr,&reordered).executable);
 // The public witness may be stale or forged; only the identity-checked private
 // certificate supplies a witness. It must not authorize a changed AST.
 auto forged=std::make_shared<grammar::DerivationMetadata>(*admitted.derivation);
 forged->nodes.clear();forged->choices.clear();admitted.derivation=forged;
 same_bytecode(layout.executable,import_native_phase_layout(grammar,admitted).executable);
 admitted.ast.consts.push_back(Value::invalid());rejected=false;
 try{(void)import_native_phase_layout(grammar,admitted);}catch(const std::invalid_argument&){rejected=true;}
 require(rejected,"import reused a private certificate after AST mutation");
}
}
int main(int argc,char** argv) {try {
 require(argc==1 || argc==3,"usage: test_gpu_phase_layout [PREPARED_JSON GRAMMAR_JSON]");
 const auto grammar_owner=std::make_shared<const grammar::CompiledGrammar>(grammar::compile_grammar(grammar::load_definition(argc==3?argv[2]:
     GAGP_REPOSITORY_ROOT "/cpp/tests/fixtures/phase_layout_memo.json")));
 const auto& grammar=*grammar_owner;
 std::vector<ProgramGenome> population;
 if(argc==3) {
  std::ifstream file(argv[1]);require(bool(file),"missing prepared input");
  const std::string text((std::istreambuf_iterator<char>(file)),{});
  const auto document=cli_detail::JsonParser(text,{true,512}).parse();
  for(const auto& row:document.object_v.at("programs").array_v) {ProgramGenome g;g.ast=cli_detail::decode_ast_json(row);population.push_back(std::move(g));}
 } else for(unsigned seed=0;seed<64;++seed)population.push_back(grammar::generate_derivation(grammar,seed).genome);
 NativePhaseGrammarProfiles profiles(grammar_owner);
 for(const auto& genome:population) {
  check_import(grammar,genome);
  const auto cached=import_native_phase_layout(grammar,genome,&profiles);
  const auto uncached=import_native_phase_layout(grammar,genome);
  same_bytecode(cached.executable,uncached.executable);
  require(ast_cache_key(export_native_phase_layout(grammar,cached,cached.genes).ast)==
      ast_cache_key(export_native_phase_layout(grammar,uncached,uncached.genes).ast),"cached grammar changed imported AST");
 }
 const auto foreign=grammar::compile_grammar(grammar::load_definition(argc==3?argv[2]:
     GAGP_REPOSITORY_ROOT "/cpp/tests/fixtures/phase_layout_memo.json"));
 bool wrong_owner=false;
 try{(void)import_native_phase_layout(foreign,population.front(),&profiles);}catch(const std::invalid_argument& e){wrong_owner=std::string(e.what()).find("owner mismatch")!=std::string::npos;}
 require(wrong_owner,"grammar profile cache accepted foreign owner");
 if(argc==1) {
  const auto coupled=grammar::compile_grammar(grammar::parse_definition(test::bounded_capture_definition()));
  const auto genome=grammar::generate_derivation(coupled,17).genome;bool rejected=false;
  try{(void)import_native_phase_layout(coupled,genome);}catch(const std::invalid_argument&){rejected=true;}
  require(rejected,"coupled/lexical region combination did not request native fallback");
  std::ifstream source(GAGP_REPOSITORY_ROOT "/cpp/tests/fixtures/phase_layout_memo.json");
  const std::string fixture((std::istreambuf_iterator<char>(source)),{});
  {
    auto document=cli_detail::JsonParser(fixture).parse();
    auto& alternatives=document.object_v.at("nonterminals").array_v[1].object_v.at("alternatives").array_v;
    alternatives.push_back(cli_detail::JsonParser(R"({"id":"list_length","weight":4,"expression":{
      "signature":"len(IntList)->Int","args":[{"signature":"concat(IntList,IntList)->IntList","args":[
        {"signature":"singleton(Int)->IntList","args":[{"ref":"Expr"}]},
        {"constant":{"type":"IntList","values":[[]]}}]}]}})").parse());
    const auto lists=grammar::compile_grammar(grammar::parse_definition(grammar::canonical_json(document)));
    bool saw_empty=false;
    for(unsigned seed=0;seed<64;++seed) {
      const auto member=grammar::generate_derivation(lists,seed).genome;
      for(const auto& value:member.ast.consts)saw_empty|=value.tag==ValueTag::IntList;
      check_import(lists,member,false); // independent membership and exact lowering roundtrip
    }
    require(saw_empty,"list grammar test failed to exercise payload constants");
    auto& literal=alternatives.back().object_v.at("expression").object_v.at("args").array_v[0]
        .object_v.at("args").array_v[1].object_v.at("constant").object_v.at("values").array_v[0];
    literal=cli_detail::JsonParser(R"(["1"])").parse();
    // The phase profile materializes payload constants of any value, but a
    // bounded-region genome carrying a nonempty list constant falls back.
    const auto nonempty=grammar::compile_grammar(grammar::parse_definition(grammar::canonical_json(document)));
    (void)compile_phase_grammar(nonempty,1,12);
    int declined=0;
    for(unsigned seed=0;seed<64;++seed) {
      const auto member=grammar::generate_derivation(nonempty,seed).genome;
      const bool listed=std::any_of(member.ast.consts.begin(),member.ast.consts.end(),
          [](const Value& v){return v.tag==ValueTag::IntList && Value::container_len(v)>0;});
      if(!listed)continue;
      try{(void)import_native_phase_layout(nonempty,member);}catch(const std::invalid_argument&){++declined;continue;}
      require(false,"nonempty list constant silently admitted to a bounded-region phase");
    }
    require(declined>0,"list grammar test failed to exercise nonempty payload constants");
  }
  for (const auto* charge : {R"({"nodes":2,"depth":1,"resets_depth":false})",
                            R"({"nodes":1,"depth":0,"resets_depth":true})"}) {
    auto document=cli_detail::JsonParser(fixture).parse();
    document.object_v.at("templates").array_v[0].object_v.at("body").object_v["resource_charge"]=
        cli_detail::JsonParser(charge).parse();
    const auto weighted=grammar::compile_grammar(grammar::parse_definition(grammar::canonical_json(document)));
    auto member=grammar::generate_derivation(weighted,17).genome;
    grammar::require_membership(weighted,member); // A valid grammar/member, not malformed input.
    std::string reason;
    try{(void)import_native_phase_layout(weighted,member);}catch(const std::invalid_argument& e){reason=e.what();}
    require(reason.find("weighted/reset skeleton")!=std::string::npos,"weighted/reset skeleton did not explicitly decline");
  }
  for (const bool depth_limit : {false,true}) {
    auto document=cli_detail::JsonParser(fixture).parse();
    document.object_v.at("templates").array_v[0].object_v.at("body").object_v["resource_charge"]=
        cli_detail::JsonParser(R"({"nodes":0,"depth":0,"resets_depth":false})").parse();
    const auto initial=grammar::compile_grammar(grammar::parse_definition(grammar::canonical_json(document)));
    auto member=grammar::generate_derivation(initial,17).genome;
    std::uint64_t largest=0;
    for(unsigned seed=0;seed<64;++seed) {
      auto candidate=grammar::generate_derivation(initial,seed).genome;
      VerifiedAst tree;
      (void)grammar::reconstruct_derivation(initial,candidate,grammar::entry_request(initial),&tree);
      grammar::ResourceProjection resources(tree.subtree_end);
      const auto size=depth_limit?resources.subtree().peak():candidate.ast.nodes.size();
      if(size>largest){largest=size;member=std::move(candidate);}
    }
    VerifiedAst physical;
    (void)grammar::reconstruct_derivation(initial,member,grammar::entry_request(initial),&physical);
    grammar::ResourceProjection physical_resources(physical.subtree_end);
    const auto limit=depth_limit ? physical_resources.subtree().peak()-1 : member.ast.nodes.size()-1;
    document.object_v.at("search_limits").object_v.at(depth_limit?"max_depth":"max_nodes").number_v=limit;
    const auto bounded=grammar::compile_grammar(grammar::parse_definition(grammar::canonical_json(document)));
    check_import(initial,member);
    const auto projected=grammar::project_derivation_resources(initial,member);
    require(grammar::ProjectedBudget{bounded.search_limits().max_nodes,bounded.search_limits().max_depth}.accepts(projected.subtree()),
        "zero-charge fixture must fit authored budget");
    member.derivation=std::make_shared<const grammar::DerivationMetadata>(grammar::reconstruct_derivation(initial,member));
    std::string reason;
    try{(void)import_native_phase_layout(bounded,member);}catch(const std::invalid_argument& e){reason=e.what();}
    require(reason.find(depth_limit?"materialized depth budget":"materialized node budget")!=std::string::npos,
        "physical budget overflow must decline before allowance subtraction");
  }

 }
 std::cout<<"roundtrip_programs="<<population.size()<<" all_instructions_constants_fuel_bindings_plans_exact=true\n";
 return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
