"""Strict one-shot source transformation; caller owns the shared GPU lock."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[6]


def apply(dry_run=False):
    changes = {}

    def edit(name, old, new):
        path = ROOT / name
        text = changes.get(path, path.read_text())
        assert text.count(old) == 1, (name, old[:100], text.count(old))
        changes[path] = text.replace(old, new)

    source = 'cpp/src/runtime/gpu/fitness_gpu.cu'
    edit(source, '#include <unordered_map>', '#include <type_traits>\n#include <unordered_map>')
    edit(source, '  const auto launch_prep_t0 = std::chrono::steady_clock::now();', '''  // Compilation geometry only: the arena still launches 32 physical threads
  // and keeps the same logical score lanes, workspace layout and case order.
  const char* private_launch_setting=std::getenv("GAGP_GPU_PRIVATE_LAUNCH_BOUND");
  int requested_private_launch=0;
  if(private_launch_setting) {
    const std::string value(private_launch_setting);
    if(value=="32")requested_private_launch=32;
    else if(value=="512")requested_private_launch=512;
    else if(value=="1024")requested_private_launch=1024;
    else if(value!="0")return fitness_eval_single_error(ErrCode::Value,"invalid private GPU launch bound");
  }
  const int private_launch_bound=private_intlists && requested_private_launch?
      requested_private_launch:32;
  const auto launch_prep_t0 = std::chrono::steady_clock::now();''')
    for begin, end in [
        ('      gpu_detail::evaluate_fitness_programs_impl<gpu_detail::DPayloadFlavor::IndexedArena,true>\n',
         '              dev.d_case_counts,impl_->d_arena_case_order,nullptr,indices.compact_queue);'),
        ('      gpu_detail::evaluate_fitness_programs_impl<gpu_detail::DPayloadFlavor::IndexedArena,true>\n',
         '              dev.d_case_counts,impl_->d_arena_case_order,replay_indices,replay_queue);'),
    ]:
        path = ROOT / source
        text = changes[path]
        stop = text.index(end) + len(end)
        start = text.rindex(begin, 0, stop)
        old = text[start:stop]
        kernel = old.replace('IndexedArena,true>', 'IndexedArena,true,false,decltype(bound)::value>')
        new = '      const auto launch_private=[&](auto bound) {\n' + kernel + '''
      };
      if(private_launch_bound==512)launch_private(std::integral_constant<int,512>{});
      else if(private_launch_bound==1024)launch_private(std::integral_constant<int,1024>{});
      else launch_private(std::integral_constant<int,32>{});'''
        edit(source, old, new)
    edit(source, '    if(reused_count)out.execution_profile+="-completed-fitness-reuse";', '''    if(requested_private_launch)out.execution_profile+=private_intlists && (global_payload || out.payload_replay_programs)?
        "-private-launch"+std::to_string(private_launch_bound):"-private-launch-declined";
    if(reused_count)out.execution_profile+="-completed-fitness-reuse";''')

    test = 'cpp/tests/gpu/test_bounded_region_gpu.cpp'
    edit(test, 'bool test_private_measure_cache(bool private_window=false) {',
         'bool test_private_measure_cache(bool private_window=false,int launch_bound=0) {')
    edit(test, '      leaf("GAGP_GPU_WINDOW_LEAF_DIRECT");', '''      leaf("GAGP_GPU_WINDOW_LEAF_DIRECT"),launch("GAGP_GPU_PRIVATE_LAUNCH_BOUND");
  setenv("GAGP_GPU_PRIVATE_LAUNCH_BOUND",std::to_string(launch_bound).c_str(),1);''')
    edit(test, '          on.execution_profile.find("-private-measure-cache")!=std::string::npos &&', '''          (!launch_bound || on.execution_profile.find("-private-launch"+std::to_string(launch_bound))!=std::string::npos) &&
          on.execution_profile.find("-private-measure-cache")!=std::string::npos &&''')
    edit(test, '  for(int lanes:{32,128})for(int fuel:{0,3,40,1000000}) {', '''  if(launch_bound==0) {
    FitnessSessionGpu invalid;
    if(!check(invalid.init(cases,answers,1000,32,7,policy).ok,"private launch validation init"))return false;
    setenv("GAGP_GPU_PRIVATE_LAUNCH_BOUND","511",1);
    if(!check(!invalid.eval_programs(programs,true).ok,"invalid private launch bound accepted"))return false;
    setenv("GAGP_GPU_PRIVATE_LAUNCH_BOUND","0",1);
  }
  for(int lanes:{32,128})for(int fuel:{0,3,40,1000000}) {''')
    edit(test, '  if (!test_private_measure_cache(true)) return 1;', '''  if (!test_private_measure_cache(true)) return 1;
  for(int bound:{512,1024}) {
    if (!test_private_measure_cache(false,bound) || !test_private_measure_cache(true,bound)) return 1;
  }''')
    evolution_test = 'cpp/tests/gpu/test_gpu_phase_population.cu'
    edit(evolution_test, '  const auto dirty=owner.evaluate(session,true);', '''  const auto dirty=owner.evaluate(session,true);
  const char* launch_prior=std::getenv("GAGP_GPU_PRIVATE_LAUNCH_BOUND");
  const bool launch_present=launch_prior!=nullptr;
  const std::string launch_saved=launch_prior?launch_prior:"";
  const int compile_bound=generation%2?512:1024;
  setenv("GAGP_GPU_PRIVATE_LAUNCH_BOUND",std::to_string(compile_bound).c_str(),1);
  const auto launch_tuned=owner.evaluate(session,true);
  if(launch_present)setenv("GAGP_GPU_PRIVATE_LAUNCH_BOUND",launch_saved.c_str(),1);
  else unsetenv("GAGP_GPU_PRIVATE_LAUNCH_BOUND");
  require(launch_tuned.ok && launch_tuned.fitness==gpu.fitness && launch_tuned.wrong==gpu.wrong &&
      launch_tuned.case_counts==gpu.case_counts &&
      launch_tuned.execution_profile.find("-private-launch"+std::to_string(compile_bound))!=std::string::npos,
      "private launch geometry changed cross-generation full-case evaluation");''')
    if not dry_run:
        for path, text in changes.items():
            path.write_text(text)
    return [str(path.relative_to(ROOT)) for path in changes]
