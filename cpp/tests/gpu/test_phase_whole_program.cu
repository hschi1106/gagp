// Grammars without templates run on the phase engine as one whole-program
// slot: expression entries across all eight value types, and Program entries
// with statements, loops, branches and grammar locals, lexical let binders
// (nested, shadowing, dropped by a reference), the shipped bounded-region
// grammars (memoized and recursive regions, captures, let bind fuel), and
// templates over all four traversal forms (profiled and plain fuel, nested
// traversals, lets inside step bodies, and binders hidden by a scope map). The
// phase compiler must
// lower every generated genome to the native compiler's exact bytecode, and the
// CPU reference and GPU owner must publish one search of verified members.
#include <algorithm>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include "gagp/cli/program_json.hpp"
#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "../../src/evolution/repro/gpu/phase_layout.hpp"
#include "../../src/evolution/repro/gpu/phase_population_cpu.hpp"

namespace {
using namespace gagp;using namespace gagp::evo;using namespace gagp::evo::repro;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
bool same_value(const Value& x,const Value& y) {
 if(x.tag!=y.tag)return false;
 if(x.tag==ValueTag::Bool)return x.b==y.b;
 return x.i==y.i;  // Float compares by bits; payload handles by identity.
}
void dump(const BytecodeProgram& p) {
 for(std::size_t i=0;i<p.code.size();++i)std::cerr<<i<<": op"<<int(p.code[i].op)<<" a"<<p.code[i].a<<" b"<<p.code[i].b<<" f"<<(i<p.instruction_fuel.size()?int(p.instruction_fuel[i]):-1)<<"\n";
 std::cerr<<"consts "<<p.consts.size()<<" locals "<<p.n_locals<<"\n";
}
void same_code(const std::vector<Instr>& a,const std::vector<Instr>& b,const std::vector<Value>& ac,const std::vector<Value>& bc,
    const std::vector<std::uint32_t>& af,const std::vector<std::uint32_t>& bf,int an,int bn,const std::string& label) {
 require(a.size()==b.size() && ac.size()==bc.size() && af==bf && an==bn,label+": bytecode shape differs");
 for(std::size_t i=0;i<a.size();++i){const auto x=a[i],y=b[i];
  require(x.op==y.op && x.a==y.a && x.b==y.b && x.has_a==y.has_a && x.has_b==y.has_b,label+": instruction differs");}
 for(std::size_t i=0;i<ac.size();++i)require(same_value(ac[i],bc[i]),label+": constant differs");
}
void same_phase(const RegionPhase& a,const RegionPhase& b,const std::string& label) {
 require(a.bindings.size()==b.bindings.size(),label+": phase bindings differ");
 for(std::size_t i=0;i<a.bindings.size();++i)
  require(a.bindings[i].local==b.bindings[i].local && a.bindings[i].source.bank==b.bindings[i].source.bank &&
      a.bindings[i].source.slot==b.bindings[i].source.slot,label+": phase binding differs");
 same_code(a.program.code,b.program.code,a.program.consts,b.program.consts,a.program.instruction_fuel,b.program.instruction_fuel,
     a.program.n_locals,b.program.n_locals,label+" phase");
}
void same_program(const BytecodeProgram& a,const BytecodeProgram& b,const std::string& label) {
 if(a.code.size()!=b.code.size() || a.consts.size()!=b.consts.size() || a.instruction_fuel!=b.instruction_fuel){std::cerr<<"--a\n";dump(a);std::cerr<<"--b\n";dump(b);}
 same_code(a.code,b.code,a.consts,b.consts,a.instruction_fuel,b.instruction_fuel,a.n_locals,b.n_locals,label);
 require(a.var2idx==b.var2idx && a.bounded_region_segments.size()==b.bounded_region_segments.size(),label+": program tables differ");
 for(std::size_t s=0;s<a.bounded_region_segments.size();++s) {
  const auto& x=a.bounded_region_segments[s];const auto& y=b.bounded_region_segments[s];
  require(x.parameter_locals==y.parameter_locals && bool(x.boundary)==bool(y.boundary) &&
      x.preparations.size()==y.preparations.size() && x.request_expressions.size()==y.request_expressions.size(),label+": region segment differs");
  if(x.boundary)same_phase(*x.boundary,*y.boundary,label);
  same_phase(x.base_predicate,y.base_predicate,label);same_phase(x.base_body,y.base_body,label);same_phase(x.combine,y.combine,label);
  for(std::size_t i=0;i<x.preparations.size();++i)same_phase(x.preparations[i],y.preparations[i],label);
  for(std::size_t i=0;i<x.request_expressions.size();++i)same_phase(x.request_expressions[i],y.request_expressions[i],label);
 }
}
// Phase lowering of the imported gene, patched into the prototype executable.
BytecodeProgram phase_lowered(const NativePhaseLayout& layout,const grammar::CompiledGrammar& grammar,
    const std::vector<std::string>& names) {
 ValueTag locals[64];
 for(std::size_t j=0;j<names.size();++j)for(const auto& input:grammar.inputs())if(input.name==names[j])locals[j]=phase_value_tag(input.type);
 const auto& gene=layout.genes.at(0);const auto& regions=layout.profiles.at(0).regions;
 std::vector<gpu_detail::DInstr> code(4096);std::vector<Value> values(4096);
 const auto r=compile_phase_tree<true,true>(gene.nodes.data(),int(gene.nodes.size()),gene.constants.data(),int(gene.constants.size()),
     locals,int(names.size()),layout.program_terminator?phase_value_tag(grammar.nonterminals().at(layout.slots.at(0).nonterminal).type):kPhaseStatement,
     code.data(),int(code.size()),values.data(),int(values.size()),regions.data(),int(regions.size()));
 require(r.status==PhaseCompileStatus::Ok && r.stack_bound>=1 && r.stack_bound<=gpu_detail::MAX_STACK,"phase compiler rejected a grammar program");
 auto program=layout.executable;patch_whole_program(program,layout,code.data(),r.code_count,values.data(),r.constant_count,r);
 return program;
}
// One value per input type (a second Int input differs), and an expected
// value of the entry type.
std::string cases_json(const grammar::CompiledGrammar& grammar) {
 std::ostringstream o;o<<"{\"format_version\":\"fitness-cases\",\"cases\":[";
 const char* words[]={"ab","Hello","","zz9","banana"};
 for(int i=0;i<48;++i) {
  if(i)o<<',';
  o<<"{\"inputs\":{";int ints=0;
  for(std::size_t k=0;k<grammar.inputs().size();++k) {
   const auto& input=grammar.inputs()[k];if(k)o<<',';o<<"\""<<input.name<<"\":";
   switch(input.type) {
    case RType::Float:o<<"{\"type\":\"float\",\"value\":"<<(i-24)*0.25<<"}";break;
    case RType::Int:o<<"{\"type\":\"int\",\"value\":"<<(ints++?(i/3+ints)%6:i%9-4)<<"}";break;
    case RType::String:o<<"{\"type\":\"string\",\"value\":\""<<words[i%5]<<"\"}";break;
    case RType::IntList:o<<"{\"type\":\"int_list\",\"value\":["<<i%3<<','<<i%5-2<<"]}";break;
    case RType::FloatList:o<<"{\"type\":\"float_list\",\"value\":["<<i*0.5<<"]}";break;
    case RType::StringList:o<<"{\"type\":\"string_list\",\"value\":[\""<<words[(i+1)%5]<<"\",\""<<words[(i+3)%5]<<"\"]}";break;
    case RType::Char:o<<"{\"type\":\"char\",\"value\":\""<<char('a'+i%26)<<"\"}";break;
    case RType::Bool:o<<"{\"type\":\"bool\",\"value\":"<<(i%3==0?"true":"false")<<"}";break;
    default:throw std::runtime_error("unsupported fixture input type");
   }
  }
  o<<"},\"expected\":";
  switch(grammar.nonterminals().at(grammar.entry()).type) {
   case RType::Int:o<<"{\"type\":\"int\",\"value\":"<<(i%7)*(i%5)<<"}";break;
   case RType::IntList:o<<"{\"type\":\"int_list\",\"value\":["<<i%4<<','<<i%3<<"]}";break;
   case RType::StringList:o<<"{\"type\":\"string_list\",\"value\":[\""<<words[i%5]<<"\"]}";break;
   default:o<<"{\"type\":\"float\",\"value\":"<<(i-24)*(i-24)*0.0625+1<<"}";
  }
  o<<"}";
 }
 o<<"]}";return o.str();
}
struct Trace{std::vector<std::string> asts;std::vector<double> fitness,best,mean;std::string profile;};
Trace trace(const EvolutionResult& r) {
 Trace t;t.profile=r.reproduction_profile;
 for(const auto& m:r.final_population){t.asts.push_back(cli_detail::encode_ast_json(m.genome.ast));t.fitness.push_back(m.fitness);}
 t.best=r.history_best_fitness;t.mean=r.history_mean_fitness;return t;
}
void require_same(const Trace& a,const Trace& b,const std::string& label) {
 require(a.profile.rfind("resident-cpu-",0)==0 && b.profile.rfind("resident-gpu-",0)==0,label+": phase owners not used");
 require(!a.asts.empty() && a.asts==b.asts && a.fitness==b.fitness && a.best==b.best && a.mean==b.mean,label+": CPU and GPU searches differ");
}
// Native generation verifies each program; statement grammars may read a local
// before any assignment, which the native verifier (and the phase engine) rejects.
std::optional<ProgramGenome> generated(const grammar::CompiledGrammar& grammar,unsigned seed) {
 try{return grammar::generate_derivation(grammar,seed).genome;}
 catch(const std::invalid_argument&){return std::nullopt;}
}
enum class Fixture{Expressions,Statements,Lets,Regions,Captures,Traversals,Templates};
// `fixture` is relative to the repository root. Without `search`, only
// lowering parity runs (the GPU evaluator rejects the grammar's region limits).
void check(const char* fixture,Fixture mode,bool search=true) {
 const bool statements=mode==Fixture::Statements;
 const auto compiled=std::make_shared<const grammar::CompiledGrammar>(grammar::compile_grammar(
     grammar::load_definition(std::string(GAGP_REPOSITORY_ROOT "/")+fixture)));
 std::vector<std::string> names;for(const auto& input:compiled->inputs())names.push_back(input.name);
 auto reordered=names;std::reverse(reordered.begin(),reordered.end());
 // Lowering parity over generated programs, in grammar and reversed input order.
 std::set<int> kinds;std::set<ValueTag> constant_tags;int programs=0,rejected=0,nested=0,regions=0,lexical=0,reversed=0;
 for(unsigned seed=0;seed<256;++seed) {
  const auto genome=generated(*compiled,seed);
  if(!genome){++rejected;continue;}
  ++programs;
  for(const auto* order:{&names,&reordered}) {
   const auto layout=import_native_phase_layout(*compiled,*genome,nullptr,order);
   require(layout.whole_program && layout.slots.size()==1 && layout.program_terminator!=statements,
       "grammar did not import as one whole-program slot");
   const auto native=compile_for_eval(*genome,*order);
   same_program(layout.executable,native,"import");
   same_program(phase_lowered(layout,*compiled,*order),native,"phase lowering seed "+std::to_string(seed));
   const auto restored=export_native_phase_layout(*compiled,layout,layout.genes);
   grammar::require_membership(*compiled,restored);
   same_program(compile_for_eval(restored,*order),native,"export");
  }
  for(const auto& node:genome->ast.nodes)kinds.insert(int(node.kind));
  if(genome->ast.lexical_regions.size()>=2)++nested;
  regions+=!genome->ast.bounded_region_specs.empty();
  for(const auto& region:genome->ast.bounded_region_specs)
   lexical+=std::any_of(region.parameters.begin(),region.parameters.end(),[](const auto& p){return p.kind==RegionCaptureKind::Lexical;});
  for(const auto& value:genome->ast.consts)constant_tags.insert(value.tag);
  reversed+=std::any_of(genome->ast.traversal_specs.begin(),genome->ast.traversal_specs.end(),
      [](const auto& t){return t.direction==TraversalDirection::Reverse;});
 }
 if(statements) {
  for(auto kind:{NodeKind::FOR_RANGE,NodeKind::IF_STMT,NodeKind::ASSIGN,NodeKind::RETURN,NodeKind::BLOCK_NIL,NodeKind::VAR,NodeKind::IF_EXPR})
   require(kinds.count(int(kind)),"statement fixture did not exercise node kind "+std::to_string(int(kind)));
  require(programs>=64 && rejected>0,"statement fixture must mix verified and undefined-local programs: "+std::to_string(programs)+"/"+std::to_string(rejected));
 } else if(mode==Fixture::Regions) {
  require(rejected==0 && regions>=128,"region grammar must generate bounded regions: "+std::to_string(regions));
 } else if(mode==Fixture::Captures) {
  require(rejected==0 && regions>=96 && lexical>=32 && nested>=8,"capture fixture must mix input and lexical captures under lets: "+
      std::to_string(regions)+"/"+std::to_string(lexical)+"/"+std::to_string(nested));
 } else if(mode==Fixture::Lets) {
  require(rejected==0,"let grammar generation failed");
  for(auto kind:{NodeKind::LET_REGION,NodeKind::REGION_VAR,NodeKind::CALL_LEN})
   require(kinds.count(int(kind)),"let fixture did not exercise node kind "+std::to_string(int(kind)));
  require(nested>=32,"let fixture must nest binders: "+std::to_string(nested));
 } else if(mode==Fixture::Templates) {
  require(rejected==0,"template grammar generation failed");
 } else if(mode==Fixture::Traversals) {
  require(rejected==0,"traversal grammar generation failed");
  for(auto kind:{NodeKind::TRAVERSE,NodeKind::TRAVERSE_RANGE,NodeKind::LET_REGION,NodeKind::IF_EXPR})
   require(kinds.count(int(kind)),"traversal fixture did not exercise node kind "+std::to_string(int(kind)));
  require(nested>=64 && reversed>=32,"traversal fixture must nest and reverse traversals: "+
      std::to_string(nested)+"/"+std::to_string(reversed));
 } else {
  require(rejected==0,"expression grammar generation failed");
  require(constant_tags.size()>=7,"fixture did not exercise constants of most value types");
  require(kinds.size()>=20,"fixture did not exercise enough node kinds");
 }

 if(!search){std::cout<<fixture<<": "<<programs<<" programs, lowering parity only\n";return;}
 // CPU reference and GPU owner publish one search from generated parents, from
 // a site-depth initial population, and from that search's exported population.
 const auto cases=cli_detail::decode_fitness_cases_json(cli_detail::JsonParser(cases_json(*compiled),{true,64}).parse());
 constexpr int population=64;
 EvolutionConfig base;base.population_size=population;base.generations=6;base.seed=5;base.elite_count=1;base.mutation_rate=0.3;
 base.compiled_grammar=compiled;base.generation_request=grammar::entry_request(*compiled);
 base.mutation_subtree_prob=1;base.fuel=compiled->execution_limits().fuel;base.penalty=1000;base.gpu_blocksize=32;
 base.phase_population=true;
 // Evolution prunes unreachable payloads, so every run rebuilds its parents:
 // generated, none (site-depth initializer), or decoded from exported ASTs.
 const auto run=[&](int site_depth,const std::vector<std::string>* exported,bool gpu){
  auto cfg=base;cfg.gpu_phase_site_depth=site_depth;
  if(site_depth)cfg.phase_variation={PhaseSiteDraw::Counted,PhaseSiteScope::Genome,0.9,8};
  cfg.eval_engine=gpu?EvalEngine::GPU:EvalEngine::CPU;
  cfg.reproduction_backend=gpu?ReproductionBackend::Gpu:ReproductionBackend::Cpu;
  std::vector<ProgramGenome> parents;
  if(exported)for(const auto& text:*exported){ProgramGenome g;g.ast=cli_detail::decode_ast_json(cli_detail::JsonParser(text,{true,512}).parse());parents.push_back(std::move(g));}
  else if(!site_depth)for(unsigned seed=901;int(parents.size())<population;++seed)if(auto g=generated(*compiled,seed))parents.push_back(std::move(*g));
  const auto result=evolve_population(cases,cfg,parents.empty()?nullptr:&parents);
  // Every published member, including rejected-variation fallbacks, is native valid.
  for(const auto& member:result.final_population)grammar::require_membership(*compiled,member.genome);
  return trace(result);
 };
 const std::string label=fixture;
 require_same(run(0,nullptr,false),run(0,nullptr,true),label+" generated parents");
 const auto fresh=run(7,nullptr,false);
 require(std::set<std::string>(fresh.asts.begin(),fresh.asts.end()).size()>population/4,label+": search did not vary programs");
 require_same(fresh,run(7,nullptr,true),label+" site-depth initial population");
 require_same(run(7,&fresh.asts,false),run(7,&fresh.asts,true),label+" exported population");
 std::cout<<fixture<<": "<<programs<<" programs, "<<rejected<<" undefined-local rejections\n";
}
// A grammar whose traversals nest deeper than the resident capacity is
// declined at admission; the search then reruns on the native representation
// with host reproduction, exactly as native_fallback_config describes.
void check_declined(const char* fixture) {
 const auto compiled=std::make_shared<const grammar::CompiledGrammar>(grammar::compile_grammar(
     grammar::load_definition(std::string(GAGP_REPOSITORY_ROOT "/")+fixture)));
 const auto cases=cli_detail::decode_fitness_cases_json(cli_detail::JsonParser(cases_json(*compiled),{true,64}).parse());
 EvolutionConfig cfg;cfg.population_size=32;cfg.generations=3;cfg.seed=11;cfg.elite_count=1;cfg.mutation_rate=0.3;
 cfg.compiled_grammar=compiled;cfg.generation_request=grammar::entry_request(*compiled);
 cfg.mutation_subtree_prob=1;cfg.fuel=compiled->execution_limits().fuel;cfg.penalty=1000;cfg.gpu_blocksize=32;
 cfg.phase_population=true;cfg.gpu_phase_site_depth=7;cfg.phase_variation={PhaseSiteDraw::Counted,PhaseSiteScope::Genome,0.9,8};
 cfg.phase_schedule=PhaseSchedulePolicy{PhaseScheduleMode::SteadyState,2,32};
 for(const bool gpu:{false,true}) {
  auto run=cfg;
  run.eval_engine=gpu?EvalEngine::GPU:EvalEngine::CPU;
  run.reproduction_backend=gpu?ReproductionBackend::Gpu:ReproductionBackend::Cpu;
  const auto label=std::string(fixture)+(gpu?" GPU":" CPU");
  const auto result=evolve_population(cases,run);
  require(result.resident_declined.find("lowering capacity")!=std::string::npos,label+": resident engine was not declined: "+result.resident_declined);
  require(result.reproduction_profile.rfind("resident-",0)!=0,label+": declined run used a resident owner");
  const auto native=native_fallback_config(run);
  require(!native.phase_population.value_or(true) && !native.gpu_phase_site_depth && !native.phase_variation && !native.phase_schedule &&
      native.reproduction_backend==ReproductionBackend::Cpu && native.eval_engine==run.eval_engine,label+": fallback config");
  const auto direct=evolve_population(cases,native);
  require(direct.resident_declined.empty(),label+": native run reported a decline");
  const auto a=trace(result),b=trace(direct);
  require(!a.asts.empty() && a.asts==b.asts && a.fitness==b.fitness && a.best==b.best,label+": fallback differs from the native search");
  for(const auto& member:result.final_population)grammar::require_membership(*compiled,member.genome);
 }
 std::cout<<fixture<<": declined, native fallback identical on CPU and GPU evaluation\n";
}
}  // namespace

int main() { try {
 check("cpp/tests/fixtures/phase_whole_program.json",Fixture::Expressions);
 check("cpp/tests/fixtures/phase_statements.json",Fixture::Statements);
 check("cpp/tests/fixtures/phase_let.json",Fixture::Lets);
 check("cpp/tests/fixtures/phase_region.json",Fixture::Captures);
 check("configs/grammar/compat/dp1d_int.json",Fixture::Regions);
 check("configs/grammar/compat/dp2d_int.json",Fixture::Regions);
 // A 1024-cell memo table exceeds the GPU evaluator's region capacity, and a
 // grammar of one fixed program has nothing to search.
 for(const char* grammar:{"configs/grammar_definitions/bounded_memo.json","configs/grammar_definitions/bounded_sequence.json",
     "configs/grammar/examples/acyclic_memo.json","configs/grammar/examples/changed_recursive_split.json"})
  check(grammar,Fixture::Regions,false);
 check("cpp/tests/fixtures/phase_traversal.json",Fixture::Traversals);
 // Shipped templates: a fold whose step and last holes are the only choices,
 // and a single program with a repeated hole of fixed fill.
 check("configs/grammar/compat/linear_rec_intlist_int.json",Fixture::Templates,false);
 check("configs/grammar/examples/authoring/template.json",Fixture::Templates,false);
 check_declined("cpp/tests/fixtures/phase_declined.json");
 std::cout<<"phase whole-program: lowering parity and CPU/GPU searches identical\n";
 return 0;
} catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;} }
