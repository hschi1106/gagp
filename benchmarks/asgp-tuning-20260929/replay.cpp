// Fixed-input tuning harness. Production libraries and original comparison remain unchanged.
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include "gagp/cli/commands.hpp"
#include "gagp/evolution/compiler.hpp"
#include "gagp/evolution/evolve.hpp"
#include "gagp/evolution/grammar/definition.hpp"
#include "gagp/evolution/grammar/membership.hpp"
#include "gagp/runtime/gpu/fitness_gpu.hpp"
#include "gagp/runtime/payload/payload.hpp"
namespace gg=gagp::evo::grammar;
using J=gagp::cli_detail::JsonValue;
std::string read(const char* path){std::ifstream f(path);if(!f)throw std::runtime_error(path);std::ostringstream s;s<<f.rdbuf();return s.str();}
J number(int n){J v;v.kind=J::Kind::Number;v.number_v=n;return v;}
int main(int argc,char** argv)try{
  if(argc!=6)throw std::runtime_error("replay INPUTS GRAMMAR FRAMES BLOCKSIZE eval|generation");
  const int frames=std::stoi(argv[3]), block=std::stoi(argv[4]);const std::string scope=argv[5];
  if(frames<50||frames>128||block<32||block>1024||(scope!="eval"&&scope!="generation"))throw std::runtime_error("unsupported experiment setting");
  auto raw=gagp::cli_detail::JsonParser(read(argv[1]),{true,512}).parse();
  std::vector<gagp::evo::ProgramGenome> population;
  std::vector<gagp::BytecodeProgram> baseline_code;
  for(const auto& a:raw.object_v.at("gagp_ast").array_v){
    gagp::evo::ProgramGenome g;g.ast=gagp::cli_detail::decode_ast_json(a);g.meta=gagp::evo::build_genome_meta(g.ast);
    if(g.ast.bounded_region_specs.size()!=1||g.ast.bounded_region_specs[0].plan.memoized)throw std::runtime_error("expected single nonmemoized DC region");
    baseline_code.push_back(gagp::evo::compile_for_eval(g,{"source"}));population.push_back(std::move(g));
  }
  std::vector<gagp::evo::EvalCase> cases;
  for(const auto& c:raw.object_v.at("cases").array_v){std::vector<gagp::Value> values;
    for(const auto& v:c.object_v.at("input").array_v)values.push_back(gagp::Value::from_int(std::stoll(v.string_v)));
    // These frozen DC cases are nonempty, length <= 50. Every split decreases length;
    // no phase can recurse. Thus at most 50 live region frames are needed.
    if(values.empty()||values.size()>50)throw std::runtime_error("case outside frame-bound proof");
    cases.push_back({{{"source",gagp::payload::make_int_list_value(values)}},gagp::Value::from_int(std::stoll(c.object_v.at("expected").string_v))});
  }
  if(population.size()!=1024||cases.size()!=1024)throw std::runtime_error("experiment fixes population=cases=1024");
  auto cs=gagp::evo::prepare_case_set(cases);
  gagp::FitnessSessionGpu baseline;auto init=baseline.init(cs.bindings,cs.expected_values,2000000,256,1000);
  if(!init.ok)throw std::runtime_error(init.err.message);
  auto reference=baseline.eval_programs(baseline_code);if(!reference.ok)throw std::runtime_error(reference.err.message);
  auto definition=gg::load_definition(argv[2]);
  for(auto& t:definition.document.object_v.at("templates").array_v)
    t.object_v.at("body").object_v.at("structured").object_v.at("plan").object_v.at("limits").object_v["frames"]=number(frames);
  auto grammar=std::make_shared<gg::CompiledGrammar>(gg::compile_grammar(gg::parse_definition(gg::canonical_json(definition.document))));
  std::vector<gagp::BytecodeProgram> code;
  for(auto& g:population){g.ast.bounded_region_specs[0].plan.limits.frames=frames;g.meta=gagp::evo::build_genome_meta(g.ast);
    g.derivation=std::make_shared<gg::DerivationMetadata>(gg::reconstruct_derivation(*grammar,g));
    code.push_back(gagp::evo::compile_for_eval(g,{"source"}));}
  gagp::FitnessSessionGpu session;init=session.init(cs.bindings,cs.expected_values,2000000,block,1000);
  if(!init.ok)throw std::runtime_error(init.err.message);
  auto check=session.eval_programs(code);if(!check.ok)throw std::runtime_error(check.err.message);
  if(check.fitness!=reference.fitness)throw std::runtime_error("tuning changed GPU fitness vector");
  std::cerr<<"PASS exact GPU fitness vector; frames="<<frames<<" block="<<block<<" grammar="<<grammar->content_hash()<<'\n';
  std::cout<<"frames,block,mode,rep,total_ms,eval_ms,repro_ms,kernel_ms,pack_ms,upload_ms,copyback_ms,decode_ms\n";
  for(int rep=-1;rep<3;++rep){
    if(scope=="eval"){
      auto fit=session.eval_programs(code);if(!fit.ok||fit.fitness!=reference.fitness)throw std::runtime_error("repeated eval mismatch");auto&t=fit.timing;
      std::cout<<frames<<','<<block<<",eval,"<<rep<<','<<t.total_ms<<','<<t.total_ms<<",0,"<<t.kernel_ms<<','<<t.pack_ms<<','<<t.upload_ms<<','<<t.copyback_ms<<",0\n"<<std::flush;
    }else for(int i=0;i<2;++i){bool overlap=(i+rep+1)%2;
      gagp::evo::EvolutionConfig cfg;cfg.population_size=1024;cfg.generations=1;cfg.seed=0;cfg.compiled_grammar=grammar;cfg.generation_request=gg::entry_request(*grammar);
      cfg.eval_engine=gagp::evo::EvalEngine::GPU;cfg.reproduction_backend=gagp::evo::repro::ReproductionBackend::Gpu;cfg.repro_overlap=overlap;
      cfg.gpu_blocksize=block;cfg.fuel=2000000;cfg.penalty=1000;cfg.mutation_rate=.3;cfg.mutation_subtree_prob=1;cfg.skip_final_eval=true;cfg.retain_final_population=false;
      auto result=gagp::evo::evolve_population(cases,cfg,&population);auto&t=result.timing.generations[0];
      std::cout<<frames<<','<<block<<','<<(overlap?"on":"off")<<','<<rep<<','<<t.total_ms<<','<<t.eval_ms<<','<<t.repro_ms<<','<<t.evaluation.gpu_eval_kernel_ms<<','<<t.evaluation.gpu_eval_pack_ms<<','<<t.evaluation.gpu_eval_upload_ms<<','<<t.evaluation.gpu_eval_copyback_ms<<','<<t.reproduction.decode_ms<<'\n'<<std::flush;
    }
  }
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
