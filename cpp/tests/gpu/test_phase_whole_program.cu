// Grammars without bounded regions run on the phase engine as one whole-program
// slot: expression entries across all eight value types, and Program entries
// with statements, loops, branches and grammar locals. The phase compiler must
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
void same_program(const BytecodeProgram& a,const BytecodeProgram& b,const std::string& label) {
 if(a.code.size()!=b.code.size() || a.consts.size()!=b.consts.size() || a.instruction_fuel!=b.instruction_fuel){std::cerr<<"--a\n";dump(a);std::cerr<<"--b\n";dump(b);}
 require(a.code.size()==b.code.size() && a.consts.size()==b.consts.size() && a.n_locals==b.n_locals &&
     a.var2idx==b.var2idx && a.instruction_fuel==b.instruction_fuel && a.bounded_region_segments.empty() &&
     b.bounded_region_segments.empty(),label+": bytecode shape differs");
 for(std::size_t i=0;i<a.code.size();++i){const auto x=a.code[i],y=b.code[i];
  require(x.op==y.op && x.a==y.a && x.b==y.b && x.has_a==y.has_a && x.has_b==y.has_b,label+": instruction differs");}
 for(std::size_t i=0;i<a.consts.size();++i)require(same_value(a.consts[i],b.consts[i]),label+": constant differs");
}
// Phase lowering of the imported gene, patched into the prototype executable.
BytecodeProgram phase_lowered(const NativePhaseLayout& layout,const grammar::CompiledGrammar& grammar,
    const std::vector<std::string>& names) {
 ValueTag locals[64];
 for(std::size_t j=0;j<names.size();++j)for(const auto& input:grammar.inputs())if(input.name==names[j])locals[j]=phase_value_tag(input.type);
 const auto& gene=layout.genes.at(0);
 std::vector<gpu_detail::DInstr> code(4096);std::vector<Value> values(4096);
 const auto r=compile_phase_tree<true,true>(gene.nodes.data(),int(gene.nodes.size()),gene.constants.data(),int(gene.constants.size()),
     locals,int(names.size()),layout.program_terminator?phase_value_tag(grammar.nonterminals().at(layout.slots.at(0).nonterminal).type):kPhaseStatement,
     code.data(),int(code.size()),values.data(),int(values.size()));
 require(r.status==PhaseCompileStatus::Ok && r.stack_bound>=1 && r.stack_bound<=gpu_detail::MAX_STACK,"phase compiler rejected a grammar program");
 auto program=layout.executable;patch_whole_program(program,layout,code.data(),r.code_count,values.data(),r.constant_count,r);
 return program;
}
std::string cases_json() {
 std::ostringstream o;o<<"{\"format_version\":\"fitness-cases\",\"cases\":[";
 const char* words[]={"ab","Hello","","zz9","banana"};
 for(int i=0;i<48;++i) {
  if(i)o<<',';
  o<<"{\"inputs\":{\"x\":{\"type\":\"float\",\"value\":"<<(i-24)*0.25<<"},"
   <<"\"n\":{\"type\":\"int\",\"value\":"<<i%9-4<<"},"
   <<"\"s\":{\"type\":\"string\",\"value\":\""<<words[i%5]<<"\"},"
   <<"\"xs\":{\"type\":\"int_list\",\"value\":["<<i%3<<','<<i%5-2<<"]},"
   <<"\"fs\":{\"type\":\"float_list\",\"value\":["<<i*0.5<<"]},"
   <<"\"ws\":{\"type\":\"string_list\",\"value\":[\""<<words[(i+1)%5]<<"\",\""<<words[(i+3)%5]<<"\"]},"
   <<"\"c\":{\"type\":\"char\",\"value\":\""<<char('a'+i%26)<<"\"},"
   <<"\"b\":{\"type\":\"bool\",\"value\":"<<(i%3==0?"true":"false")<<"}},"
   <<"\"expected\":{\"type\":\"float\",\"value\":"<<(i-24)*(i-24)*0.0625+1<<"}}";
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
 require(a.profile.rfind("native-cpu-phase-",0)==0 && b.profile.rfind("native-gpu-phase-",0)==0,label+": phase owners not used");
 require(!a.asts.empty() && a.asts==b.asts && a.fitness==b.fitness && a.best==b.best && a.mean==b.mean,label+": CPU and GPU searches differ");
}
// Native generation verifies each program; statement grammars may read a local
// before any assignment, which the native verifier (and the phase engine) rejects.
std::optional<ProgramGenome> generated(const grammar::CompiledGrammar& grammar,unsigned seed) {
 try{return grammar::generate_derivation(grammar,seed).genome;}
 catch(const std::invalid_argument&){return std::nullopt;}
}
void check(const char* fixture,bool statements) {
 const auto compiled=std::make_shared<const grammar::CompiledGrammar>(grammar::compile_grammar(
     grammar::load_definition(std::string(GAGP_REPOSITORY_ROOT "/cpp/tests/fixtures/")+fixture)));
 std::vector<std::string> names;for(const auto& input:compiled->inputs())names.push_back(input.name);
 auto reordered=names;std::reverse(reordered.begin(),reordered.end());
 // Lowering parity over generated programs, in grammar and reversed input order.
 std::set<int> kinds;std::set<ValueTag> constant_tags;int programs=0,rejected=0;
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
  for(const auto& value:genome->ast.consts)constant_tags.insert(value.tag);
 }
 if(statements) {
  for(auto kind:{NodeKind::FOR_RANGE,NodeKind::IF_STMT,NodeKind::ASSIGN,NodeKind::RETURN,NodeKind::BLOCK_NIL,NodeKind::VAR,NodeKind::IF_EXPR})
   require(kinds.count(int(kind)),"statement fixture did not exercise node kind "+std::to_string(int(kind)));
  require(programs>=64 && rejected>0,"statement fixture must mix verified and undefined-local programs: "+std::to_string(programs)+"/"+std::to_string(rejected));
 } else {
  require(rejected==0,"expression grammar generation failed");
  require(constant_tags.size()>=7,"fixture did not exercise constants of most value types");
  require(kinds.size()>=20,"fixture did not exercise enough node kinds");
 }

 // CPU reference and GPU owner publish one search from generated parents, from
 // a site-depth initial population, and from that search's exported population.
 const auto cases=cli_detail::decode_fitness_cases_json(cli_detail::JsonParser(cases_json(),{true,64}).parse());
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
}  // namespace

int main() { try {
 check("phase_whole_program.json",false);
 check("phase_statements.json",true);
 std::cout<<"phase whole-program: lowering parity and CPU/GPU searches identical\n";
 return 0;
} catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;} }
