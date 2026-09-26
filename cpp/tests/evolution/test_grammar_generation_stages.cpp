#include <iostream>
#include <memory>
#include <stdexcept>
#include "gagp/cli/grammar_artifact.hpp"
#include "gagp/evolution/grammar/donor.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/grammar/variation.hpp"
#include "gagp/evolution/mutation.hpp"
#include "gagp/runtime/payload/payload.hpp"
using namespace gagp;
using namespace gagp::evo;
using namespace gagp::evo::grammar;
namespace {
using Json = cli_detail::JsonValue;
Json json(const std::string& text) { return cli_detail::JsonParser(text, {true, 256}).parse(); }
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F f) {
  try { f(); } catch (const std::invalid_argument&) { return; }
  throw std::runtime_error("invalid stage/request accepted");
}
Json definition() { return json(R"({
  "format_version":"grammar-definition-v2","entry":{"nonterminal":"Root","type":"Int"},
  "search_limits":{"max_nodes":12,"max_depth":7},"execution_limits":{"fuel":100},
  "nonterminals":[
    {"id":"Root","type":"Int","scope":[],"alternatives":[
      {"id":"alias","weight":1,"expression":{"ref":"Leaf"}}]},
    {"id":"Leaf","type":"Int","scope":[],"alternatives":[
      {"id":"initial","weight":1,"generation_stages":["initial"],
       "expression":{"constant":{"type":"Int","values":["1"]}}},
      {"id":"mutation","weight":1,"generation_stages":["mutation"],
       "expression":{"constant":{"type":"Int","values":["2"]}}},
      {"id":"member","weight":1000,"generation_stages":[],
       "expression":{"constant":{"type":"Int","values":["3"]}}}
    ]}
  ]})"); }
std::shared_ptr<const CompiledGrammar> compile(const Json& d) {
  return std::make_shared<const CompiledGrammar>(compile_grammar(parse_definition(canonical_json(d))));
}
std::int64_t leaf(const ProgramGenome& g) { return g.ast.consts.at(g.ast.nodes.at(3).i0).i; }
void test_generation_membership_replay() {
  const auto grammar = compile(definition());
  auto initial = entry_request(*grammar), mutation = initial;
  mutation.stage = GenerationStage::Mutation;
  for (std::uint64_t seed=0;seed<64;++seed) {
    auto a=generate_derivation(*grammar,seed,initial);
    auto b=generate_derivation(*grammar,seed,mutation);
    check(leaf(a.genome)==1 && leaf(b.genome)==2,"stage admitted wrong terminal domain");
    require_membership(*grammar,a.genome,mutation);
    require_membership(*grammar,b.genome,initial);
    auto member=a.genome; member.ast.consts.at(member.ast.nodes.at(3).i0)=Value::from_int(3);
    require_membership(*grammar,member,initial);
    auto encoded=cli_detail::encode_generated_artifact(*grammar,b);
    auto replay=cli_detail::replay_generated_artifact(encoded);
    check(replay.derivation.request.stage==GenerationStage::Mutation && leaf(replay.genome)==2,
          "replay lost generation stage");
    auto changed=json(encoded);
    changed.object_v.at("request").object_v.erase("generation_stage");
    rejects([&]{ (void)cli_detail::replay_generated_artifact(canonical_json(changed)); });
    check(!json(cli_detail::encode_generated_artifact(*grammar,a)).object_v.at("request").object_v.count("generation_stage"),
          "default artifact encoding changed");
  }
  auto invalid=initial; invalid.stage=static_cast<GenerationStage>(255);
  rejects([&]{ (void)validate_request(*grammar,invalid); });
  for (const auto& stages : {R"(["typo"])",R"(["initial","initial"])",R"("initial")",R"([0])"}) {
    auto d=definition();
    d.object_v.at("nonterminals").array_v[1].object_v.at("alternatives").array_v[0].object_v["generation_stages"]=json(stages);
    rejects([&]{ (void)compile(d); });
  }
}
void test_cache_donor_and_variation() {
  auto d=definition();
  d.object_v["locals"]=json(R"([{"name":"x","type":"Int"}])");
  // A reachable but unavailable local requires distinct contextual stage tables.
  d.object_v.at("nonterminals").array_v[1].object_v.at("alternatives").array_v.push_back(
      json(R"({"id":"local","weight":1,"expression":{"local":"x"}})"));
  const auto grammar=compile(d);
  VariationContext context(grammar);
  auto initial=entry_request(*grammar), mutation=initial; mutation.stage=GenerationStage::Mutation;
  GenerationFrame frame;
  for(int i=0;i<2;++i) {
    check(leaf(generate_derivation_in_frame(context,7,initial,frame).genome)==1,"initial frame cache contaminated");
    check(leaf(generate_derivation_in_frame(context,7,mutation,frame).genome)==2,"mutation frame cache contaminated");
  }
  check(context.frame_cost_cache_counters().misses==2 && context.frame_cost_cache_counters().hits==2,
        "frame cache failed to separate stages");
  auto parent=generate_derivation_in_frame(context,7,initial,frame).genome;
  CompatibilityRegistry registry;
  const auto a=analyze_variation(*grammar,parent,initial,&registry);
  const auto b=analyze_variation(*grammar,parent,mutation,&registry);
  check(a.sites.size()==b.sites.size() && !a.sites.empty(),"stage changed membership sites");
  for(std::size_t i=0;i<a.sites.size();++i) {
    check(a.sites[i].compatibility_id==b.sites[i].compatibility_id,"stage changed crossover compatibility");
    check(donor_request(a.sites[i]).stage==GenerationStage::Mutation,"donor request lost stage");
    check(leaf(generate_donor(context,7,a.sites[i]).genome)==2,"donor used initial alternatives");
  }
  VariationAnalysisCache cache(grammar);
  const auto x=cache.analyze(parent,initial), y=cache.analyze(parent,mutation);
  check(x!=y && x->witness.request.stage==GenerationStage::Initial &&
        y->witness.request.stage==GenerationStage::Mutation,"analysis cache lost request provenance");
  auto child=mutate(parent,12,context,1.0);
  check(leaf(child)==2 && context.counters().acceptance_rejections==0,"CPU subtree mutation ignored stage");
}
void test_local_free_frame_costs() {
  auto d=definition();
  d.object_v["locals"]=json(R"([{"name":"x","type":"Int"}])");
  d.object_v.at("nonterminals").array_v.push_back(json(R"(
    {"id":"Unrelated","type":"Int","scope":[],"alternatives":[
      {"id":"local","weight":1,"expression":{"local":"x"}}]})"));
  const auto grammar=compile(d);
  VariationContext context(grammar);
  GenerationFrame frame;
  for(auto stage:{GenerationStage::Initial,GenerationStage::Mutation}) {
    auto request=entry_request(*grammar); request.stage=stage;
    const auto plain=generate_derivation(*grammar,7,request);
    const auto framed=generate_derivation_in_frame(context,7,request,frame);
    auto expected = plain.genome.ast;
    expected.names.clear();  // This fixture generates only a constant expression.
    check(framed.genome.ast.names.empty(), "local-free donor retained unused grammar names");
    check(ast_cache_key(expected)==ast_cache_key(framed.genome.ast),
          "local-free frame changed seeded generation");
  }
  check(context.frame_cost_cache_counters().misses==0,
        "unreachable local forced contextual cost construction");
}
void test_default_sampling_and_sequence_domains() {
  auto d=definition();
  for(auto& rule:d.object_v.at("nonterminals").array_v)
    for(auto& alt:rule.object_v.at("alternatives").array_v) alt.object_v.erase("generation_stages");
  const auto implicit=compile(d);
  for(auto& rule:d.object_v.at("nonterminals").array_v)
    for(auto& alt:rule.object_v.at("alternatives").array_v)
      alt.object_v["generation_stages"]=json(R"(["initial","mutation"])");
  const auto explicit_both=compile(d);
  auto mutation=entry_request(*explicit_both); mutation.stage=GenerationStage::Mutation;
  for(unsigned seed=0;seed<256;++seed) {
    const auto a=generate_derivation(*implicit,seed);
    const auto b=generate_derivation(*explicit_both,seed,mutation);
    check(ast_cache_key(a.genome.ast)==ast_cache_key(b.genome.ast) &&
          a.derivation.logical_steps==b.derivation.logical_steps,"default sampling trajectory changed");
  }
  d=definition(); d.object_v.at("entry").object_v.at("type")=json("\"StringList\"");
  for(auto& rule:d.object_v.at("nonterminals").array_v) rule.object_v.at("type")=json("\"StringList\"");
  auto& alternatives=d.object_v.at("nonterminals").array_v[1].object_v.at("alternatives").array_v;
  alternatives.resize(2);
  alternatives[0].object_v.at("expression")=json(R"({"constant":{"type":"StringList","sequence":{
    "length":[5,5],"element":{"type":"String","sequence":{"length":[8,8],"element":{"type":"Char","values":["a"]}}}}}})");
  alternatives[1].object_v.at("expression")=json(R"({"constant":{"type":"StringList","sequence":{
    "length":[4,4],"element":{"type":"String","sequence":{"length":[5,5],"element":{"type":"Char","values":["b"]}}}}}})");
  const auto grammar=compile(d);
  for(auto stage:{GenerationStage::Initial,GenerationStage::Mutation}) {
    auto request=entry_request(*grammar); request.stage=stage;
    const auto g=generate_derivation(*grammar,1,request).genome;
    std::vector<Value> elements;
    check(payload::lookup_list(g.ast.consts.at(g.ast.nodes.at(3).i0),&elements),"missing staged list");
    check(elements.size()==(stage==GenerationStage::Initial?5:4),"wrong stage list length");
    for(auto value:elements)
      check(Value::container_len(value)==(stage==GenerationStage::Initial?8:5),"wrong stage element length");
    require_membership(*grammar,g);
  }
}
void test_shared_template_stage_costs() {
  auto d=definition();
  d.object_v["templates"]=json(R"([{"id":"Pair","type":"Int","scope":[],
    "holes":[{"id":"x","type":"Int","scope":[]}],
    "body":{"signature":"add(Int,Int)->Int","args":[{"hole":"x"},{"hole":"x"}]}}])");
  auto& rules=d.object_v.at("nonterminals").array_v;
  rules[0].object_v.at("alternatives").array_v[0].object_v.at("expression")=
    json(R"({"template":"Pair","holes":{"x":{"ref":"Leaf"}}})");
  rules[1].object_v.at("alternatives").array_v[1].object_v.at("expression")=
    json(R"({"signature":"add(Int,Int)->Int","args":[
      {"constant":{"type":"Int","values":["2"]}},{"constant":{"type":"Int","values":["2"]}}]})");
  const auto grammar=compile(d);
  auto request=entry_request(*grammar); request.budget={7,5};
  check(generate_derivation(*grammar,7,request).genome.ast.nodes.size()==7,"initial shared-hole cost changed");
  request.stage=GenerationStage::Mutation;
  rejects([&]{ (void)generate_derivation(*grammar,7,request); });
  request.budget={11,6};
  const auto generated=generate_derivation(*grammar,7,request);
  check(generated.genome.ast.nodes.size()==11,"mutation shared-hole multiplicity missing");
  for(auto c:generated.genome.ast.consts) check(c.i==2,"shared hole generated another stage");
}

void test_stage_feasibility_and_alias_cycles() {
  auto d=definition();
  auto& alternatives=d.object_v.at("nonterminals").array_v[1].object_v.at("alternatives").array_v;
  alternatives[1].object_v.at("expression")=json(R"({"signature":"add(Int,Int)->Int","args":[
      {"constant":{"type":"Int","values":["2"]}},{"constant":{"type":"Int","values":["2"]}}]})");
  auto grammar=compile(d);
  auto request=entry_request(*grammar); request.budget={5,4};
  (void)generate_derivation(*grammar,0,request);
  request.stage=GenerationStage::Mutation;
  // Union membership is feasible, but the mutation production requires three nodes.
  (void)validate_request(*grammar,request);
  rejects([&]{ (void)generate_derivation(*grammar,0,request); });
  request.budget={7,5};
  check(generate_derivation(*grammar,0,request).genome.ast.nodes.size()==7,"stage budget rejected exact fit");
  // A union-productive alias cycle has an initial exit but no mutation exit.
  d=definition();
  auto& rules=d.object_v.at("nonterminals").array_v;
  auto& alts=rules[1].object_v.at("alternatives").array_v;
  alts.resize(1);
  alts.push_back(json(R"({"id":"cycle","weight":1,"generation_stages":["mutation"],"expression":{"ref":"Root"}})"));
  grammar=compile(d); request=entry_request(*grammar);
  check(leaf(generate_derivation(*grammar,0,request).genome)==1,"initial alias exit failed");
  request.stage=GenerationStage::Mutation;
  rejects([&]{ (void)generate_derivation(*grammar,0,request); });
  // Entirely membership-only nonterminals are valid but cannot generate.
  for(auto& rule:rules) for(auto& alt:rule.object_v.at("alternatives").array_v)
    alt.object_v["generation_stages"]=json("[]");
  grammar=compile(d); request=entry_request(*grammar);
  rejects([&]{ (void)generate_derivation(*grammar,0,request); });
}
}
int main() {
  try { test_generation_membership_replay(); test_cache_donor_and_variation();
        test_stage_feasibility_and_alias_cycles(); test_default_sampling_and_sequence_domains();
        test_shared_template_stage_costs(); test_local_free_frame_costs();
        std::cout<<"generation stages passed\n"; }
  catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
