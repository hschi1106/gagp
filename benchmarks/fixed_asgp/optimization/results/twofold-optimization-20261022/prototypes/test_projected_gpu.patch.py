replace('cpp/tests/gpu/test_bounded_region_gpu.cpp','#include "../../src/runtime/gpu/view_profile.hpp"',
    '#include "../../src/runtime/gpu/view_profile.hpp"\n#include "../../src/runtime/gpu/shape_result_cache.hpp"')
path=root/'cpp/tests/gpu/test_bounded_region_gpu.cpp';text=changes.get(path,path.read_text())
marker='int main() {\n  payload::clear();'
addition=r'''bool test_projected_shape_inputs() {
  struct Restore {
    struct Entry{const char* name;bool present;std::string value;};std::vector<Entry> entries;
    explicit Restore(std::initializer_list<const char*> names) {for(const auto* name:names) {
      const auto* value=std::getenv(name);entries.push_back({name,bool(value),value?value:""});setenv(name,"1",1);
    }}
    ~Restore(){for(const auto& e:entries)if(e.present)setenv(e.name,e.value.c_str(),1);else unsetenv(e.name);}
  } restore({"GAGP_VIEW_PROFILE","GAGP_TYPED_VIEW_PHASE","GAGP_DIRECT_PHASE","GAGP_DIRECT_ROOT",
      "GAGP_COMPACT_FRAMES","GAGP_WINDOW_EXECUTOR","GAGP_UNBOXED_WINDOW_FRAMES","GAGP_WINDOW_SOA",
      "GAGP_SORT_CASES","GAGP_MEASURE_PHASE_CACHE","GAGP_GPU_SHAPE_RESULT_CACHE",
      "GAGP_GPU_PROJECT_SHAPE_INPUTS","GAGP_GPU_RELAXED_VIEW_REGISTERS"});
  auto segment=sequence_segment(ValueTag::IntList);
  segment.plan.result_type=ValueTag::Int;segment.plan.preparations.resize(1);segment.preparations.resize(1);
  segment.plan.requests.resize(2);
  segment.plan.requests[1].states[0]=window(0,endpoint(WindowEndpointKind::InteriorCut,0),endpoint(WindowEndpointKind::End));
  segment.plan.parameter_types={ValueTag::Int};segment.parameter_locals={1};
  segment.base_body=constant_phase(Value::from_int(7));
  // This declared capture is unused; its distinct scalar values cannot affect the root.
  segment.base_body.bindings={{{RegionSlotBank::Parameter,0},0}};segment.base_body.program.n_locals=1;
  segment.combine=phase({}, {ins_a(Opcode::Load,0),ins_a(Opcode::Load,1),ins(Opcode::Add),ins(Opcode::Return)},
      {{slot(RegionSlotBank::Result,0),0},{slot(RegionSlotBank::Result,1),1}},2);
  auto unused=invocation(segment,{payload::make_int_list_value({Value::from_int(0)})},3);
  unused.code[0]=ins_a(Opcode::Load,0);
  auto used=unused;used.bounded_region_segments[0].base_body=load_phase(RegionSlotBank::Parameter,0);
  auto unreachable=unused;
  unreachable.bounded_region_segments[0].base_predicate=constant_phase(Value::from_bool(true));
  unreachable.bounded_region_segments[0].combine=load_phase(RegionSlotBank::Parameter,0);
  const std::vector<BytecodeProgram> programs{unused,used,unreachable};
  for(const auto& program:programs)if(!verify_fixture(program,"projected capture"))return false;
  const auto packed=gpu_detail::pack_programs_with_shared_case_count(programs,1024,0);
  const std::uint64_t required[]{1,3,1};
  for(int i=0;i<3;++i) {
    std::uint64_t inputs=0;
    if(!check(gpu_detail::shape_result_program_supported(packed,packed.metas[i],&inputs) && inputs==required[i],
        "fresh phase dependency mask included unused or omitted used caller input"))return false;
  }
  std::vector<CaseBindings> cases;std::vector<Value> answers;
  for(int i=0;i<1024;++i) {
    std::vector<Value> values(i%8,Value::from_int(i*31));
    cases.push_back({{0,payload::make_int_list_value(values)},{1,Value::from_int(i)},
                     {2,Value::from_float(i+.125)}});
    answers.push_back(Value::from_int(i%17));
  }
  const FitnessPolicy policy{FitnessMetric::Absolute,FitnessProximity::Discounted};
  for(int fuel:{0,1,3,16,1000}) {
    FitnessSessionGpu reference,cached;
    unsetenv("GAGP_GPU_SHAPE_RESULT_CACHE");
    if(!reference.init(cases,answers,fuel,512,7,policy).ok)return false;
    setenv("GAGP_GPU_SHAPE_RESULT_CACHE","1",1);
    if(!cached.init(cases,answers,fuel,512,7,policy).ok)return false;
    const auto a=reference.eval_programs(programs,true),b=cached.eval_programs(programs,true);
    if(!check(a.ok && b.ok && a.fitness==b.fitness && b.fitness==eval_fitness_cpu(programs,cases,answers,fuel,7,512,policy) &&
        a.wrong==b.wrong && a.case_counts==b.case_counts && b.shape_result_programs==2 &&
        b.shape_result_groups==8 && b.physical_program_cases==1040,
        "projection ignored used capture, read unused Float or merged per-case targets"))return false;
    if(fuel==1000) {
      auto changed=unused;
      changed.bounded_region_segments[0].base_body.program.consts[0]=Value::from_int(13);
      for(const auto& current:std::vector<BytecodeProgram>{unused,used,changed,unreachable,unused}) {
        const auto result=cached.eval_programs({current},true);
        if(!check(result.ok && result.fitness==eval_fitness_cpu({current},cases,answers,fuel,7,512,policy),
            "partition cache reused a previous program result or dependency mask"))return false;
      }
    }
  }
  // Missing/wrong USED captures force ordinary admission/execution and retain errors.
  cases.resize(128);answers.resize(128);
  for(int i=0;i<128;++i)if(i%2)cases[i][1].value=Value::from_float(i+.25);else cases[i].erase(cases[i].begin()+1);
  FitnessSessionGpu mismatch;
  if(!mismatch.init(cases,answers,1000,128,7,policy).ok)return false;
  const auto used_bad=mismatch.eval_programs({used},true);
  if(!check(used_bad.ok && used_bad.shape_result_programs==0 &&
      used_bad.fitness==eval_fitness_cpu({used},cases,answers,1000,7,128,policy),
      "used missing/wrong capture lost native name/type behavior"))return false;
  // The same invalid input locals are safe to ignore when not captured or read.
  auto plain=unused;auto& plain_segment=plain.bounded_region_segments[0];
  plain_segment.plan.parameter_types.clear();plain_segment.parameter_locals.clear();
  plain_segment.base_body=constant_phase(Value::from_int(7));
  const auto ignored=mismatch.eval_programs({plain},true);
  return check(ignored.ok && ignored.shape_result_programs==1 && ignored.shape_result_groups==8 &&
      ignored.fitness==eval_fitness_cpu({plain},cases,answers,1000,7,128,policy),
      "unused invalid caller locals prevented or changed proven projected execution");
}

'''
assert text.count(marker)==1
changes[path]=text.replace(marker,addition+marker).replace('  if (!test_shape_result_cache()) return 1;',
    '  if (!test_shape_result_cache() || !test_projected_shape_inputs()) return 1;')
replace('cpp/tests/gpu/test_gpu_phase_population.cu',
    'setenv("GAGP_GPU_SHAPE_RESULT_CACHE","1",1);FitnessSessionGpu shape_cached;',
    'setenv("GAGP_GPU_PROJECT_SHAPE_INPUTS","1",1);setenv("GAGP_GPU_SHAPE_RESULT_CACHE","1",1);FitnessSessionGpu shape_cached;')
