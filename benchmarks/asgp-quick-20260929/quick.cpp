// Read-only adapters around the unchanged ASGP and GAGP libraries.
#include <algorithm>
#include <sys/wait.h>
#include <unistd.h>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include "core/thread_pool.h"
#include "gp/operators.h"
#include "gp/fitness.h"
#include "task/registry.h"
#include "gagp/cli/commands.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/generate.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/evolve.hpp"
#include "gagp/runtime/payload/payload.hpp"
#include "gagp/runtime/cpu/execute_bytecode_cpu.hpp"
#include "gagp/runtime/gpu/fitness_gpu.hpp"
namespace gg = gagp::evo::grammar;
using J = gagp::cli_detail::JsonValue;
using Clock = std::chrono::steady_clock;
J str(std::string s) { J j; j.kind=J::Kind::String;j.string_v=std::move(s);return j; }
J num(double n) { J j; j.kind=J::Kind::Number;j.number_v=n;return j; }
J boolean(bool b) { J j; j.kind=J::Kind::Bool;j.bool_v=b;return j; }
J arr(std::vector<J> a) { J j;j.kind=J::Kind::Array;j.array_v=std::move(a);return j; }
J obj(std::initializer_list<std::pair<const std::string,J>> x) { J j;j.kind=J::Kind::Object;j.object_v=x;return j; }
J constant(std::int64_t n) { return obj({{"constant",obj({{"type",str("Int")},{"values",arr({str(std::to_string(n))})}})}}); }
J bc(bool b) { return obj({{"constant",obj({{"type",str("Bool")},{"values",arr({boolean(b)})}})}}); }
J bound(std::string s) { return obj({{"bound",str(s)}}); }
J call(std::string s,std::vector<J> args) {return obj({{"signature",str(s)},{"args",arr(std::move(args))}});}
J remainder(J a,J b){return call("imod0(Int,Int)->Int",{a,b});}
std::string read(std::string p){ std::ifstream f(p); if(!f)throw std::runtime_error(p);std::ostringstream s;s<<f.rdbuf();return s.str(); }
void save(std::string p,const J& j){std::ofstream f(p);f<<gg::canonical_json(j)<<'\n';}
double ms(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
J translate(const asgp::Expr& e,size_t& at,int phase){
  using K=asgp::NodeKind;
  auto n=e.at(at++); std::vector<J> a;
  for(int k=0;k<asgp::Arity(n.kind);++k)a.push_back(translate(e,at,phase));
  switch(n.kind){
    case K::kIntConst:return constant(n.value);
    case K::kBoolConst:return bc(n.value);
    case K::kInputList:return bound("xs");
    case K::kInputSize:return bound("n");
    case K::kResult:return bound(n.op?"right":"left");
    case K::kHead:return call("index(IntList,Int)->Int",{a.at(0),constant(0)});
    case K::kIndex:return call("index(IntList,Int)->Int",{bound("xs"),a.at(0)});
    case K::kNot:return call("not(Bool)->Bool",a);
    case K::kIntOp:{if(n.op==4)return remainder(a[0],a[1]);const char* ops[]={"add","sub","mul","idiv0","imod0"};return call(std::string(ops[n.op])+"(Int,Int)->Int",a);}
    case K::kBoolOp:{const char* ops[]={"lt","eq","gt","le","ge","ne"};return call(std::string(ops[n.op])+"(Int,Int)->Bool",a);}
    case K::kLogicOp:return n.op==0?call("if(Bool,Bool,Bool)->Bool",{a[0],a[1],bc(false)}):call("if(Bool,Bool,Bool)->Bool",{a[0],bc(true),a[1]});
    case K::kIf:return call("if(Bool,Int,Int)->Int",a);
    default:throw std::runtime_error("unsupported ASGP node "+std::to_string(int(n.kind)));
  }
}
J translated_root(const asgp::Individual& ind){
  J holes=obj({{"source",obj({{"input",str("source")}})}});
  const char* names[]={"solve","divide","combine"};
  for(int k=0;k<3;++k){size_t at=0;holes.object_v[names[k]]=translate(ind.phases[k],at,k);if(at!=ind.phases[k].size())throw std::runtime_error("trailing nodes");}
  return obj({{"template",str("Package.DC.IntList.Int")},{"holes",holes}});
}
J raw_ind(const asgp::Individual& ind){
  std::vector<J> phases;
  for(int k=0;k<3;++k){std::vector<J> nodes;for(auto n:ind.phases[k])nodes.push_back(arr({num(int(n.kind)),num(int(n.type)),num(n.op),num(n.size),str(std::to_string(n.value))}));phases.push_back(arr(nodes));}
  return arr(phases);
}
int main(int argc,char**argv)try{
  if(argc!=7)throw std::runtime_error("quick GRAMMAR POP CASES SEED REPEATS OUTPUT_PREFIX");
  const int count=std::stoi(argv[2]), nc=std::stoi(argv[3]), seed=std::stoi(argv[4]), repeats=std::stoi(argv[5]);
  const std::string prefix=argv[6];
  auto definition=gg::load_definition(argv[1]);
  auto grammar=std::make_shared<gg::CompiledGrammar>(gg::compile_grammar(definition));
  asgp::DataConfig data;data.train=nc;data.test=1;data.seed=20260626;data.legacy_finite=false;
  auto spec=*asgp::BuildTask("sum_of_elements",data);
  if(const char* limit=std::getenv("ASGP_CASE_MAX_LENGTH"))for(auto& c:spec.train){c.elements.resize(std::min(c.elements.size(),size_t(std::stoi(limit))));c.expected.i=std::accumulate(c.elements.begin(),c.elements.end(),int64_t(0));}
  auto ag=asgp::SchemeGrammar::Build(spec.Grammar());
  asgp::Rng rng(seed);std::vector<asgp::Individual> original;
  std::vector<gagp::evo::ProgramGenome> population;std::vector<gagp::BytecodeProgram> code;
  std::vector<J> raw,ast;
  auto prep=Clock::now();
  for(int i=0;i<count;++i){
    const bool controlled=std::getenv("ASGP_CONTROLLED")!=nullptr;
    auto ind=asgp::Initialize(ag,controlled?2+i%3:2+i%6,controlled?(i/3)%2==1:(i/6)%2==1,rng);
    if(controlled)for(auto& phase:ind.phases)for(auto& n:phase)if(n.kind==asgp::NodeKind::kIntOp && n.op==uint8_t(asgp::IntOp::kMul))n.op=uint8_t(asgp::IntOp::kAdd);
    auto d=definition.document;
    auto nts=d.object_v.at("nonterminals").array_v;
    auto root=*std::find_if(nts.begin(),nts.end(),[](const J& j){return j.object_v.at("id").string_v=="Root";});
    root.object_v.at("alternatives").array_v.at(0).object_v["expression"]=translated_root(ind);
    d.object_v["nonterminals"]=arr({root});
    auto exact=gg::compile_grammar(gg::parse_definition(gg::canonical_json(d)));
    auto g=gg::generate_derivation(exact,0).genome;
    std::vector<gagp::Value> unique;std::vector<int> remap;
    for(auto v:g.ast.consts){auto it=std::find_if(unique.begin(),unique.end(),[&](auto u){return u.tag==v.tag && (v.tag==gagp::ValueTag::Bool?u.b==v.b:u.i==v.i);});remap.push_back(it-unique.begin());if(it==unique.end())unique.push_back(v);}
    for(auto& n:g.ast.nodes)if(n.kind==gagp::evo::NodeKind::CONST)n.i0=remap.at(n.i0);
    g.ast.consts=std::move(unique);g.meta=gagp::evo::build_genome_meta(g.ast);
    g.derivation=std::make_shared<gg::DerivationMetadata>(gg::reconstruct_derivation(*grammar,g));
    raw.push_back(raw_ind(ind));ast.push_back(gagp::cli_detail::JsonParser(gagp::cli_detail::encode_ast_json(g.ast)).parse());
    code.push_back(gagp::evo::compile_for_eval(g,{"source"}));
    original.push_back(std::move(ind));population.push_back(std::move(g));
    if(i%256==255)std::cerr<<"translated "<<i+1<<"/"<<count<<'\n';
  }
  std::vector<gagp::evo::EvalCase> cases;std::vector<J> raw_cases;
  for(auto& c:spec.train){std::vector<gagp::Value> values;std::vector<J> js;for(auto v:c.elements){values.push_back(gagp::Value::from_int(v));js.push_back(str(std::to_string(v)));}
    cases.push_back({{{"source",gagp::payload::make_int_list_value(values)}},gagp::Value::from_int(c.expected.i)});
    raw_cases.push_back(obj({{"input",arr(js)},{"expected",str(std::to_string(c.expected.i))}}));}
  auto cs=gagp::evo::prepare_case_set(cases);
  save(prefix+".inputs.json",obj({{"asgp_population",arr(raw)},{"gagp_ast",arr(ast)},{"cases",arr(raw_cases)}}));
  std::cerr<<"translation_ms "<<ms(prep)<<'\n';
  asgp::FitnessConfig fc;fc.limits.max_steps=2000000;fc.limits.max_recursion=128;
  std::vector<double> reference(count);asgp::FitnessScratch scratch;
  for(int i=0;i<count;++i)reference[i]=-asgp::Score(original[i],spec,spec.train,fc,scratch).error;
  // Optional CPU oracle is isolated: a runtime arithmetic trap must not kill the measured backends.
  std::vector<double> cpu;int cpu_signal=0;
  const pid_t child=fork();if(child<0)throw std::runtime_error("CPU audit fork failed");
  if(child==0){
    auto values=gagp::eval_fitness_cpu(code,cs.bindings,cs.expected_values,2000000,1000,256);
    std::ofstream output(prefix+".cpu-audit.bin",std::ios::binary);
    output.write(reinterpret_cast<const char*>(values.data()),values.size()*sizeof(double));
    output.close();_exit(output?0:2);
  }
  int status=0;if(waitpid(child,&status,0)<0)throw std::runtime_error("CPU audit wait failed");
  if(WIFEXITED(status)&&WEXITSTATUS(status)==0){
    cpu.resize(count);std::ifstream input(prefix+".cpu-audit.bin",std::ios::binary);
    if(!input.read(reinterpret_cast<char*>(cpu.data()),cpu.size()*sizeof(double)))throw std::runtime_error("incomplete CPU audit");
  }else{cpu_signal=WIFSIGNALED(status)?WTERMSIG(status):-1;std::cerr<<"CPU audit failed signal="<<cpu_signal<<"; timing continues\n";}

  gagp::FitnessSessionGpu session;auto init=session.init(cs.bindings,cs.expected_values,2000000,256,1000);
  if(!init.ok)throw std::runtime_error(init.err.message);
  auto gpu=session.eval_programs(code);if(!gpu.ok)throw std::runtime_error(gpu.err.message);
  int cpu_bad=cpu.empty()?-1:0,gpu_bad=0,raw_bad=0;std::vector<J> bad;
  for(int i=0;i<count;++i){if(!cpu.empty())cpu_bad+=cpu[i]!=reference[i];gpu_bad+=gpu.fitness[i]!=reference[i];
    if((!cpu.empty() && cpu[i]!=reference[i]) || gpu.fitness[i]!=reference[i])std::cerr<<"fitness mismatch "<<i<<" asgp="<<reference[i]<<" cpu="<<(cpu.empty()?0:cpu[i])<<" gpu="<<gpu.fitness[i]<<"\n";
    asgp::DcProgram p{&original[i].phases[0],&original[i].phases[1],&original[i].phases[2],asgp::Type::kInt,1};
    for(size_t c=0;c<cases.size() && i<16;++c){const auto& e=spec.train[c];asgp::Instance x{e.elements.data(),uint32_t(e.elements.size()),e.scalars.data()};
      auto a=asgp::RunDc(p,x,fc.limits,scratch.arena);
      auto g=gagp::execute_bytecode_cpu(code[i],{{0,cases[c].inputs.at("source")}},2000000);
      if(a.ok()==g.is_error || (a.ok() && (g.value.tag!=gagp::ValueTag::Int || a.value.i!=g.value.i))){
        ++raw_bad;if(bad.size()<10)bad.push_back(obj({{"program",num(i)},{"case",num(c)},{"asgp_ok",boolean(a.ok())},{"gagp_error",boolean(g.is_error)},{"asgp_value",str(std::to_string(a.value.i))},{"gagp_value",str(std::to_string(g.value.i))}}));
      }
    }
  }
  save(prefix+".parity.json",obj({{"programs",num(count)},{"cases",num(cases.size())},{"cpu_fitness_mismatches",num(cpu_bad)},{"cpu_diagnostic_signal",num(cpu_signal)},{"gpu_fitness_mismatches",num(gpu_bad)},{"raw_result_mismatches",num(raw_bad)},{"raw_programs_checked",num(std::min(count,16))},{"examples",arr(bad)}}));
  std::cerr<<"PARITY cpu="<<cpu_bad<<" gpu="<<gpu_bad<<" raw="<<raw_bad<<'\n';
  // The user accepts numerical differences; retain the full mismatch audit.
  std::ofstream results(prefix+".timings.jsonl");
  auto emit=[&](J row){auto line=gg::canonical_json(row);results<<line<<'\n'<<std::flush;std::cout<<line<<'\n'<<std::flush;};
  std::vector<std::unique_ptr<asgp::ThreadPool>> pools;
  for(int t:{1,10,20})pools.push_back(std::make_unique<asgp::ThreadPool>(t));
  std::vector<asgp::FitnessScratch> workers(20);
  for(int rep=-1;rep<repeats;++rep){
    for(int z=0;z<5;++z){int mode=(z+rep+1)%5;
      if(mode<3){auto work=original;auto& pool=*pools[mode];auto start=Clock::now();
        pool.ParallelFor(work.size(),[&](int w,size_t i){work[i].fitness=asgp::Score(work[i],spec,spec.train,fc,workers[w]);});double eval=ms(start);
        asgp::Rng r(seed);asgp::VariationScratch vs;std::vector<asgp::Individual> children;children.reserve(count);
        auto pick=[&]()->const asgp::Individual& {auto a=r.Index(work.size()),b=r.Index(work.size());return asgp::Better(work[a].fitness,work[b].fitness)?work[a]:work[b];};
        children.push_back(*std::min_element(work.begin(),work.end(),[](auto&a,auto&b){return asgp::Better(a.fitness,b.fitness);}));
        while(children.size()<work.size()){auto& a=pick();auto& b=pick();children.push_back(asgp::CrossoverByPhase(a,b,3,.7,asgp::Reach::kOnePhase,std::getenv("ASGP_CONTROLLED")?4:7,r,vs));asgp::MutateByPhase(children.back(),ag,.3,asgp::Reach::kOnePhase,std::getenv("ASGP_CONTROLLED")?4:7,r,vs);}
        double total=ms(start);emit(obj({{"mode",str("asgp_"+std::to_string(pool.workers()))},{"rep",num(rep)},{"eval_ms",num(eval)},{"generation_ms",num(total)}}));
      }else{
        gagp::evo::EvolutionConfig cfg;cfg.population_size=count;cfg.generations=1;cfg.seed=seed;
        cfg.eval_engine=gagp::evo::EvalEngine::GPU;cfg.reproduction_backend=gagp::evo::repro::ReproductionBackend::Gpu;
        cfg.repro_overlap=mode==4;cfg.compiled_grammar=grammar;cfg.generation_request=gg::entry_request(*grammar);
        cfg.fuel=2000000;cfg.penalty=1000;cfg.gpu_blocksize=256;cfg.mutation_rate=.3;cfg.mutation_subtree_prob=1;
        cfg.skip_final_eval=true;cfg.retain_final_population=false;
        auto out=gagp::evo::evolve_population(cases,cfg,&population);const auto&t=out.timing.generations.at(0);
        emit(obj({{"mode",str(mode==4?"gagp_on":"gagp_off")},{"rep",num(rep)},{"eval_ms",num(t.eval_ms)},{"generation_ms",num(t.total_ms)},{"run_ms",num(out.timing.total_ms)},{"gpu_call_ms",num(t.evaluation.gpu_eval_call_ms)},{"compile_ms",num(t.evaluation.gpu_compile_ms)},{"repro_ms",num(t.repro_ms)},{"prep_ms",num(t.reproduction.preprocess_ms)},{"pack_ms",num(t.reproduction.pack_ms)},{"decode_ms",num(t.reproduction.decode_ms)}}));
      }
    }
    auto t=Clock::now();auto f=session.eval_programs(code);double elapsed=ms(t);if(!f.ok||f.fitness!=gpu.fitness)throw std::runtime_error("GPU repeated evaluation changed");
    emit(obj({{"mode",str("gagp_eval_precompiled")},{"rep",num(rep)},{"eval_ms",num(elapsed)}}));
  }
  return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
