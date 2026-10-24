"""Stage a generic sparse-view launch rule, independently of dense arena code."""
import difflib

import serial
from stage_dense_candidate import replace


def main():
    # These hunks do not overlap native-dense3.patch. Apply only after that
    # immutable attempt and its source-dependent searches have finished.
    paths = ["cpp/src/runtime/gpu/fitness_gpu.cu", "cpp/tests/gpu/test_bounded_region_gpu.cpp"]
    before = {p: (serial.ROOT / p).read_text() for p in paths}
    after = dict(before)
    path = paths[0]
    after[path] = replace(after[path],
        "  if(virtual_view_lanes)execution_threads=std::min(execution_threads,requested_physical_block);\n",
        "  if(virtual_view_lanes)execution_threads=std::min(execution_threads,requested_physical_block);\n"
        "  const auto* case_block_setting=std::getenv(\"GAGP_GPU_CASE_VIEW_BLOCK\");\n"
        "  const bool request_case_block=case_block_setting && std::string(case_block_setting)==\"1\";\n"
        "  // Only shrink a sparse view launch: fewer than one quarter of its\n"
        "  // logical lanes can own a case. Keep the original score slots/order.\n"
        "  const bool case_view_block=request_case_block && use_views && impl_->shared_case_count>0 &&\n"
        "      static_cast<long long>(impl_->shared_case_count)*4<fitness_lanes;\n"
        "  if(case_view_block) {\n"
        "    int cap=32;while(cap<impl_->shared_case_count)cap*=2;\n"
        "    execution_threads=std::min(execution_threads,cap);\n"
        "  }\n")
    after[path] = replace(after[path],
        "    if(request_register_stack)out.execution_profile+=region_workspace.register_phase_stack?\"-register-phase-stack\":\"-register-phase-stack-declined\";\n",
        "    if(request_case_block)out.execution_profile+=case_view_block?\n"
        "        \"-case-view-block\"+std::to_string(execution_threads):\"-case-view-block-declined\";\n"
        "    if(request_register_stack)out.execution_profile+=region_workspace.register_phase_stack?\"-register-phase-stack\":\"-register-phase-stack-declined\";\n")
    path = paths[1]
    old = '''    const char* names[2]={"GAGP_GPU_VIEW_BOUND128","GAGP_GPU_VIEW_PHYSICAL_BLOCK"};
    std::string saved[2];bool present[2];
    RestoreGeometry(){for(int i=0;i<2;++i){const auto* p=std::getenv(names[i]);present[i]=p;saved[i]=p?p:"";setenv(names[i],"0",1);}}
    ~RestoreGeometry(){for(int i=0;i<2;++i)if(present[i])setenv(names[i],saved[i].c_str(),1);else unsetenv(names[i]);}
'''
    new = old.replace('names[2]', 'names[3]').replace('saved[2]', 'saved[3]').replace('present[2]', 'present[3]')
    new = new.replace('"GAGP_GPU_VIEW_PHYSICAL_BLOCK"}', '"GAGP_GPU_VIEW_PHYSICAL_BLOCK","GAGP_GPU_CASE_VIEW_BLOCK"}').replace('i<2', 'i<3')
    after[path] = replace(after[path], old, new)
    anchor = '''        "128-thread compilation changed fractional scores, fuel, per-case targets or diagnostic route"))return false;
'''
    test = r'''
    for(int count:{19,81,128}) {
      std::vector<CaseBindings> subset(cases.begin(),cases.begin()+count);
      std::vector<Value> expected(answers.begin(),answers.begin()+count);
      FitnessSessionGpu adaptive;
      if(!adaptive.init(subset,expected,fuel,512,7,policy).ok)return false;
      setenv("GAGP_GPU_CASE_VIEW_BLOCK","0",1);
      const auto fixed=adaptive.eval_programs(programs,true);
      setenv("GAGP_GPU_CASE_VIEW_BLOCK","1",1);
      const auto sized=adaptive.eval_programs(programs,true);
      const auto dirty=adaptive.eval_programs(programs,true);
      setenv("GAGP_GPU_CASE_VIEW_BLOCK","0",1);
      std::vector<unsigned> cpu_wrong;
      const auto reference=eval_fitness_cpu(programs,subset,expected,fuel,7,512,policy,&cpu_wrong);
      const int physical=count==19?32:count==81?128:512;
      if(!check(fixed.ok && sized.ok && dirty.ok && fixed.fitness==sized.fitness && sized.fitness==reference &&
          sized.wrong==cpu_wrong && sized.case_counts==fixed.case_counts && dirty.case_counts==sized.case_counts &&
          dirty.fitness==sized.fitness && dirty.wrong==sized.wrong && sized.physical_blocksize==physical &&
          (sized.execution_profile.find("-case-view-block-declined")==std::string::npos)==(count<128),
          "case-sized view launch changed fractional scores, original logical lanes, fuel/errors or dirty workspace"))return false;
    }
'''
    after[path] = replace(after[path], anchor, anchor + test)
    patch = serial.BASE / "native-case-view4.patch"
    assert not patch.exists()
    patch.write_text("".join("".join(difflib.unified_diff(before[p].splitlines(True), after[p].splitlines(True),
                                                        fromfile="a/" + p, tofile="b/" + p)) for p in paths))
    serial.save(serial.RAW / "case-view4-staged.json", dict(patch=str(patch), patch_sha256=serial.sha(patch),
                changed_paths=paths, prerequisite="native-dense3 build and source-dependent development must finish first",
                rule="One global rule: only views with 4*C<logical fitness lanes; physical cap=max(32,nextpower2(C)); preserve all logical lanes and canonical reduction",
                scope="old TRAINING/runtime metadata only; no task-name dispatch or new heldout"))


if __name__ == "__main__":
    main()
