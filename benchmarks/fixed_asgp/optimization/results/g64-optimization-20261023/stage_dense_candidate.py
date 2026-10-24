"""Stage an immutable dense-case patch; never edit live benchmark sources."""
import difflib
from pathlib import Path

import serial


def replace(text, before, after):
    assert text.count(before) == 1, "Source contract changed; inspect instead of guessing"
    return text.replace(before, after)


def main():
    build = serial.read(serial.RAW / "build-attempt2/manifest.json")
    assert build["status"] == "ok"
    paths = ["cpp/include/gagp/runtime/gpu/region_types_gpu.hpp",
             "cpp/src/runtime/gpu/device/kernels.cuh", "cpp/src/runtime/gpu/fitness_gpu.cu",
             "cpp/tests/gpu/test_extended_payload_gpu.cu", "cpp/tests/gpu/test_gpu_phase_population.cu"]
    before = {p: (serial.ROOT / p).read_text() for p in paths}
    assert all(serial.sha(serial.ROOT / p) == build["source_sha256"][p] for p in paths)
    after = dict(before)
    path = paths[0]
    after[path] = replace(after[path],
        "  int fitness_lanes = 0; // Original logical block width for exact reduction.\n",
        "  int fitness_lanes = 0; // Original logical block width for exact reduction.\n"
        "  bool dense_case_lanes = false; // Untiled arena: schedule nonempty lanes, preserve score slots.\n")
    path = paths[1]
    after[path] = replace(after[path],
        "  const DProgramMeta meta = metas[prog_idx];\n",
        "  const DProgramMeta meta = metas[prog_idx];\n"
        "  const bool dense_lanes = (Flavor == DPayloadFlavor::ArenaMixed || Flavor == DPayloadFlavor::IndexedArena) &&\n"
        "      workspace.dense_case_lanes && !tiled_replay && meta.case_count > 0 && meta.case_count <= fitness_lanes;\n")
    after[path] = replace(after[path],
        "  for (int logical_tid = lane_begin + tid; logical_tid < lane_end; logical_tid += blockDim.x) {\n",
        "  // Arena case chunks are contiguous floor(C*lane/L) intervals. For C<=L,\n"
        "  // case k belongs to lane ceil((k+1)*L/C)-1. Schedule only those lanes,\n"
        "  // leaving every original empty score slot at +0 for the same reduction.\n"
        "  if (dense_lanes) {\n"
        "    for (int lane = tid; lane < fitness_lanes; lane += blockDim.x) partial_scores[lane] = 0.0;\n"
        "    __syncthreads();\n"
        "  }\n"
        "  const int schedule_begin = dense_lanes ? tid : lane_begin + tid;\n"
        "  const int schedule_end = dense_lanes ? meta.case_count : lane_end;\n"
        "  for (int scheduled = schedule_begin; scheduled < schedule_end; scheduled += blockDim.x) {\n"
        "  const int logical_tid = dense_lanes ?\n"
        "      static_cast<int>(((static_cast<long long>(scheduled) + 1) * fitness_lanes - 1) / meta.case_count) : scheduled;\n")
    path = paths[2]
    after[path] = replace(after[path],
        "  const int initial_warps=(impl_->blocksize+31)/32;\n",
        "  const auto* dense_setting=std::getenv(\"GAGP_GPU_DENSE_CASE_LANES\");\n"
        "  const bool request_dense=dense_setting && std::string(dense_setting)==\"1\";\n"
        "  const bool dense_initial=request_dense && global_payload && !tiled_initial &&\n"
        "      impl_->shared_case_count>0 && impl_->shared_case_count<=impl_->blocksize;\n"
        "  const int initial_warps=(impl_->blocksize+31)/32;\n")
    after[path] = replace(after[path],
        "      out.initial_payload_tiled=tiled_initial;\n",
        "      // Replay storage is made from region_workspace and retains the ordinary\n"
        "      // mapping. Only this admitted initial arena uses dense physical lanes.\n"
        "      initial_payload.workspace.dense_case_lanes=dense_initial;\n"
        "      out.initial_payload_tiled=tiled_initial;\n")
    after[path] = replace(after[path],
        "    if(global_payload) {\n      out.execution_profile+=\"-global-arena\";\n",
        "    if(request_dense)out.execution_profile+=dense_initial?\"-dense-case-lanes\":\"-dense-case-lanes-declined\";\n"
        "    if(global_payload) {\n      out.execution_profile+=\"-global-arena\";\n")
    path = paths[3]
    test = r'''
bool test_dense_case_lanes() {
  ScopedEnvironment dense("GAGP_GPU_DENSE_CASE_LANES"),tiles("GAGP_GPU_TILED_INITIAL_PAYLOAD"),
      global("GAGP_GPU_GLOBAL_PAYLOAD"),diag("GAGP_GPU_OVERFLOW_DIAGNOSTICS");
  setenv("GAGP_GPU_GLOBAL_PAYLOAD","1",1);setenv("GAGP_GPU_OVERFLOW_DIAGNOSTICS","1",1);
  BytecodeProgram list;list.n_locals=1;
  list.code={ins_a(Opcode::Load,0),ins_ab(Opcode::CallBuiltin,static_cast<int>(BuiltinId::Singleton),1),ins(Opcode::Return)};
  list.instruction_fuel={1,1,1};
  BytecodeProgram wrong;wrong.consts={Value::from_bool(true),Value::from_int(1)};
  wrong.code={ins_a(Opcode::PushConst,0),ins_a(Opcode::PushConst,1),ins(Opcode::Add),ins(Opcode::Return)};
  wrong.instruction_fuel={1,1,1,1};
  const std::vector<BytecodeProgram> programs{list,wrong,list};
  for(int count:{1,19,33,200,513}) for(int lanes:{33,512}) for(int fuel:{2,100}) {
    std::vector<CaseBindings> cases;std::vector<Value> answers;
    for(int i=0;i<count;++i) {
      cases.push_back({{0,Value::from_int(i%3)}});
      answers.push_back(payload::make_int_list_value({Value::from_int(i%5)}));
    }
    FitnessPolicy policy{FitnessMetric::Levenshtein,FitnessProximity::Discounted};
    policy.list_allocation_limit=65536;
    std::vector<unsigned> wrong_counts;std::vector<CpuFitnessCaseCounts> counts;
    const auto cpu=eval_fitness_cpu(programs,cases,answers,fuel,2.5,lanes,policy,&wrong_counts,&counts);
    for(const char* tiled:{"0","1"}) {
      setenv("GAGP_GPU_TILED_INITIAL_PAYLOAD",tiled,1);
      setenv("GAGP_GPU_DENSE_CASE_LANES","0",1);
      FitnessSessionGpu session;if(!session.init(cases,answers,fuel,lanes,2.5,policy).ok)return false;
      const auto ordinary=session.eval_programs(programs,true);
      setenv("GAGP_GPU_DENSE_CASE_LANES","1",1);
      const auto packed=session.eval_programs(programs,true);
      const auto dirty=session.eval_programs(programs,true);
      const bool admitted=tiled[0]=='0' && count<=lanes;
      if(!check(ordinary.ok && packed.ok && dirty.ok && packed.fitness==cpu && packed.wrong==wrong_counts &&
          packed.fitness==ordinary.fitness && dirty.fitness==packed.fitness && dirty.wrong==packed.wrong &&
          packed.case_counts==ordinary.case_counts && dirty.case_counts==packed.case_counts &&
          packed.initial_payload_overflow_cases==ordinary.initial_payload_overflow_cases &&
          packed.initial_payload_overflow_lane_masks==ordinary.initial_payload_overflow_lane_masks &&
          packed.execution_profile.find("-dense-case-lanes")!=std::string::npos &&
          (packed.execution_profile.find("-dense-case-lanes-declined")==std::string::npos)==admitted,
          "dense arena scheduling changed fractional fitness, original lanes, fuel/errors or fallback"))return false;
      for(int p=0;p<3;++p)if(!check(packed.case_counts[p][0]==counts[p].cases &&
          packed.case_counts[p][1]==counts[p].errors && packed.case_counts[p][2]==counts[p].timeouts,
          "dense arena omitted cases or changed CPU status counts"))return false;
    }
  }
  return true;
}

'''
    after[path] = replace(after[path], "bool test_tiled_initial_payload() {\n", test + "bool test_tiled_initial_payload() {\n")
    after[path] = replace(after[path],
        "  if (!test_auto_initial_payload_inputs() || !test_sorted_arena_cases()",
        "  if (!test_dense_case_lanes() || !test_auto_initial_payload_inputs() || !test_sorted_arena_cases()")
    path = paths[4]
    after[path] = replace(after[path],
        " bool changed=false,saw_content_match=false,saw_simple=false;\n",
        " std::vector<CaseBindings> dense_cases(cases.begin(),cases.begin()+19);\n"
        " std::vector<Value> dense_answers(answers.begin(),answers.begin()+19);\n"
        " FitnessSessionGpu dense_session;\n"
        " require(dense_session.init(dense_cases,dense_answers,audit_fuel,512,1000,policy).ok,\"fresh dense private session\");\n"
        " bool changed=false,saw_content_match=false,saw_simple=false;\n")
    after[path] = replace(after[path],
        "  require(gpu.fitness==cpu && gpu.wrong==wrong,\"new GPU list phases differ from full-case independent CPU audit\");\n",
        "  require(gpu.fitness==cpu && gpu.wrong==wrong,\"new GPU list phases differ from full-case independent CPU audit\");\n"
        "  {\n"
        "   const char* names[]={\"GAGP_GPU_DENSE_CASE_LANES\",\"GAGP_GPU_TILED_INITIAL_PAYLOAD\"};\n"
        "   std::string saved[2];bool present[2];\n"
        "   for(int i=0;i<2;++i){const auto* p=std::getenv(names[i]);present[i]=p;saved[i]=p?p:\"\";setenv(names[i],\"0\",1);}\n"
        "   const auto before_dense=owner.evaluate(dense_session,true);\n"
        "   setenv(names[0],\"1\",1);const auto dense=owner.evaluate(dense_session,true);\n"
        "   const auto dirty_dense=owner.evaluate(dense_session,true);\n"
        "   for(int i=0;i<2;++i)if(present[i])setenv(names[i],saved[i].c_str(),1);else unsetenv(names[i]);\n"
        "   std::vector<unsigned> dense_wrong;\n"
        "   const auto dense_cpu=eval_fitness_cpu(independent,dense_cases,dense_answers,audit_fuel,1000,512,policy,&dense_wrong);\n"
        "   require(before_dense.ok && dense.ok && dirty_dense.ok && before_dense.fitness==dense.fitness &&\n"
        "       dirty_dense.fitness==dense.fitness && dense.fitness==dense_cpu && dense.wrong==dense_wrong &&\n"
        "       before_dense.case_counts==dense.case_counts && dirty_dense.case_counts==dense.case_counts &&\n"
        "       dense.execution_profile.find(\"-dense-case-lanes\")!=std::string::npos &&\n"
        "       dense.execution_profile.find(\"-dense-case-lanes-declined\")==std::string::npos,\n"
        "       \"dense scheduling changed fresh private generations, full CPU audit or dirty workspace scores\");\n"
        "  }\n")
    target = serial.BASE / "native-dense3.patch"
    assert not target.exists(), "Staged implementation is immutable"
    patch = "".join("".join(difflib.unified_diff(before[p].splitlines(True), after[p].splitlines(True),
                                               fromfile="a/" + p, tofile="b/" + p)) for p in paths)
    target.write_text(patch)
    serial.save(serial.RAW / "dense3-staged.json", dict(source_build="build-attempt2",
                patch=str(target), patch_sha256=serial.sha(target), changed_paths=paths,
                before_sha256={p: build["source_sha256"][p] for p in paths},
                scope="Only untiled initial arena C<=logical lanes; same chunk assignment and reduction; replay ordinary"))


if __name__ == "__main__":
    main()
