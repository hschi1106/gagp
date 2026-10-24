"""Stage a compiler specialization using only host-proved private layout facts."""
import difflib

import serial
from stage_dense_candidate import replace


def main():
    paths = ["cpp/src/runtime/gpu/device/kernels.cuh", "cpp/src/runtime/gpu/fitness_gpu.cu",
             "cpp/tests/gpu/test_gpu_phase_population.cu", "cpp/tests/gpu/test_extended_payload_gpu.cu",
             "cpp/tests/gpu/test_bounded_region_gpu.cpp", "cpp/tests/gpu/private_intlist_offset_checks.cuh"]
    before = {p: (serial.ROOT / p).read_text() for p in paths}
    after = dict(before)
    p = paths[0]
    after[p] = replace(after[p],
        "    int LaunchThreads = (Flavor == DPayloadFlavor::ArenaMixed || Flavor == DPayloadFlavor::IndexedArena) ? 32 : 1024>\n",
        "    int LaunchThreads = (Flavor == DPayloadFlavor::ArenaMixed || Flavor == DPayloadFlavor::IndexedArena) ? 32 : 1024,\n"
        "    bool PrivateLayout = false>\n")
    anchor = "  const int tid = static_cast<int>(threadIdx.x);\n"
    specialized = r'''  if constexpr (PrivateLayout) {
    static_assert(Flavor == DPayloadFlavor::IndexedArena && EnableRegions);
    // The host selects this instantiation only when every fact below already
    // holds. Expose those facts to inlined VM code; retain all runtime checks,
    // logical resources, scratch capacities and external immutable tables.
    workspace.private_intlists = true;
    workspace.compact_intlist_cells = true;
    workspace.private_cell_soa = false;
    workspace.private_global_offsets = true;
    workspace.private_warp_copy = true;
    workspace.trivial_phases = true;
    workspace.private_typed_phases = true;
    workspace.private_bound_phases = true;
    workspace.window_executor = true;
    workspace.coordinate_executor = false;
    workspace.unboxed_window_frames = false;
    workspace.window_soa = false;
    workspace.concat_cache_enabled = false;
    workspace.concat_cache = nullptr;
    workspace.payload_string_entries = nullptr;
    workspace.payload_list_entries = nullptr;
    workspace.payload_string_bytes = nullptr;
    workspace.payload_list_values = nullptr;
    workspace.payload_list_index = nullptr;
    workspace.payload_list_epoch_index = nullptr;
    workspace.payload_list_epoch = nullptr;
  }
'''
    after[p] = replace(after[p], anchor, specialized + anchor)
    p = paths[1]
    anchor = "  const auto* dense_tiles_setting=std::getenv(\"GAGP_GPU_DENSE_CASE_TILES\");\n"
    guard = r'''  const auto* private_layout_setting=std::getenv("GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT");
  const bool request_private_layout=private_layout_setting && std::string(private_layout_setting)=="1";
  const bool private_layout=request_private_layout && private_launch_bound==32 && private_intlists &&
      region_workspace.compact_intlist_cells && !region_workspace.private_cell_soa &&
      region_workspace.private_global_offsets && region_workspace.private_warp_copy &&
      region_workspace.trivial_phases && region_workspace.private_typed_phases &&
      region_workspace.private_bound_phases && region_workspace.window_executor;
'''
    after[p] = replace(after[p], anchor, guard + anchor)
    old = "      const auto launch_private=[&](auto bound) {\n"
    assert after[p].count(old) == 2
    after[p] = after[p].replace(old, "      const auto launch_private=[&](auto bound,auto layout) {\n")
    old = "gpu_detail::DPayloadFlavor::IndexedArena,true,false,decltype(bound)::value>"
    assert after[p].count(old) == 2
    after[p] = after[p].replace(old, "gpu_detail::DPayloadFlavor::IndexedArena,true,false,decltype(bound)::value,decltype(layout)::value>")
    old = '''      if(private_launch_bound==512)launch_private(std::integral_constant<int,512>{});
      else if(private_launch_bound==1024)launch_private(std::integral_constant<int,1024>{});
      else launch_private(std::integral_constant<int,32>{});'''
    new = '''      if(private_layout)launch_private(std::integral_constant<int,32>{},std::true_type{});
      else if(private_launch_bound==512)launch_private(std::integral_constant<int,512>{},std::false_type{});
      else if(private_launch_bound==1024)launch_private(std::integral_constant<int,1024>{},std::false_type{});
      else launch_private(std::integral_constant<int,32>{},std::false_type{});'''
    assert after[p].count(old) == 2
    after[p] = after[p].replace(old, new)
    anchor = '    if(requested_private_launch)out.execution_profile+=private_intlists && (global_payload || out.payload_replay_programs)?\n'
    after[p] = replace(after[p], anchor,
        '    if(request_private_layout)out.execution_profile+=private_layout && (global_payload || out.payload_replay_programs)?\n'
        '        "-private-layout-specialized":"-private-layout-specialized-declined";\n' + anchor)
    p = paths[2]
    anchor = '       "dense case tiles changed fresh private generations, original lane masks, replay or full CPU audit");\n'
    block = r'''
   {
    const char* flags[]={"GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT","GAGP_GPU_PRIVATE_LAUNCH_BOUND",
        "GAGP_GPU_PRIVATE_CELL_SOA","GAGP_GPU_COMPACT_INTLIST_CELLS","GAGP_GPU_PRIVATE_GLOBAL_OFFSETS",
        "GAGP_GPU_TRIVIAL_PHASES","GAGP_GPU_PRIVATE_TYPED_PHASES","GAGP_GPU_PRIVATE_BOUND_PHASES",
        "GAGP_GPU_PRIVATE_WARP_COPY","GAGP_GPU_PRIVATE_WINDOW_EXECUTOR"};
    const char* values[]={"0","32","0","1","1","1","1","1","1","1"};
    std::string saved[10];bool present[10];
    for(int i=0;i<10;++i){const auto* p=std::getenv(flags[i]);present[i]=p;saved[i]=p?p:"";setenv(flags[i],values[i],1);}
    const auto ordinary=owner.evaluate(dense_session,true);
    setenv(flags[0],"1",1);const auto specialized=owner.evaluate(dense_session,true);
    const auto dirty=owner.evaluate(dense_session,true);
    setenv(flags[8],"0",1);const auto declined=owner.evaluate(dense_session,true);
    for(int i=0;i<10;++i)if(present[i])setenv(flags[i],saved[i].c_str(),1);else unsetenv(flags[i]);
    require(ordinary.ok && specialized.ok && dirty.ok && declined.ok && ordinary.fitness==dense_cpu &&
        specialized.fitness==dense_cpu && dirty.fitness==dense_cpu && declined.fitness==dense_cpu &&
        specialized.wrong==dense_wrong && declined.wrong==dense_wrong &&
        ordinary.case_counts==specialized.case_counts && dirty.case_counts==specialized.case_counts &&
        declined.case_counts==specialized.case_counts &&
        specialized.execution_profile.find("-private-layout-specialized")!=std::string::npos &&
        (specialized.execution_profile.find("-private-layout-specialized-declined")==std::string::npos)==sequence_windows &&
        declined.execution_profile.find("-private-layout-specialized-declined")!=std::string::npos,
        "host-proved private layout specialization changed fresh native generations, CPU scores or fallback");
   }
'''
    after[p] = replace(after[p], anchor, anchor + block)
    p = paths[3]
    anchor = "bool test_auto_initial_payload_inputs() {\n"
    helper = r'''bool test_private_layout_fallbacks() {
  ScopedEnvironment specialized("GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT");
  setenv("GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT","1",1);
  // Public/mixed payloads and unadmitted layouts retain their ordinary route.
  // These helpers independently check CPU scores, fractional loss, low fuel,
  // unsupported value types and initial/replay tile boundaries.
  return test_extended_payload() && test_dense_case_tiles();
}

'''
    after[p] = replace(after[p], anchor, helper + anchor)
    after[p] = replace(after[p], "  if (!test_dense_case_tiles() || !test_dense_case_lanes()",
                       "  if (!test_private_layout_fallbacks() || !test_dense_case_tiles() || !test_dense_case_lanes()")
    p = paths[1]
    old = """  const bool request_case_block=case_block_setting && std::string(case_block_setting)=="1";
  // Only shrink a sparse view launch: fewer than one quarter of its
  // logical lanes can own a case. Keep the original score slots/order.
  const bool case_view_block=request_case_block && use_views && impl_->shared_case_count>0 &&
      static_cast<long long>(impl_->shared_case_count)*4<fitness_lanes;
  if(case_view_block) {
    int cap=32;while(cap<impl_->shared_case_count)cap*=2;
    execution_threads=std::min(execution_threads,cap);
  }
"""
    new = """  const bool hybrid_view_block=case_block_setting && std::string(case_block_setting)=="hybrid";
  const bool request_case_block=case_block_setting && (std::string(case_block_setting)=="1" || hybrid_view_block);
  const bool sparse_view_cases=impl_->shared_case_count>0 &&
      static_cast<long long>(impl_->shared_case_count)*4<fitness_lanes;
  const bool case_view_block=request_case_block && use_views && impl_->shared_case_count>0 &&
      (sparse_view_cases || hybrid_view_block);
  if(case_view_block) {
    // One runtime rule, without task names: sparse cases use a small block;
    // ordinary additive phases retain wide lanes; complex scalar phases can
    // expose more independent program blocks. Logical reduction stays fixed.
    int cap=fitness_lanes;
    if(sparse_view_cases){cap=32;while(cap<impl_->shared_case_count)cap*=2;}
    else if(hybrid_view_block && !std::any_of(packed.region_phases.begin(),packed.region_phases.end(),
        [](const auto& phase){return phase.program.add_left_binding>=0;}))cap=128;
    execution_threads=std::min(execution_threads,cap);
  }
"""
    after[p] = replace(after[p], old, new)
    after[p] = replace(after[p],
        '  const bool request_dense_tiles=dense_tiles_setting && std::string(dense_tiles_setting)=="1";\n',
        '  const bool tuple_dense_tiles=dense_tiles_setting && std::string(dense_tiles_setting)=="tuple";\n'
        '  const bool request_dense_tiles=dense_tiles_setting && (std::string(dense_tiles_setting)=="1" || tuple_dense_tiles);\n'
        '  const bool dense_tiles_policy=!tuple_dense_tiles || (private_intlists && private_tuple_policy);\n')
    after[p] = replace(after[p], '  const bool dense_initial_tiles=request_dense_tiles && tiled_initial &&\n',
        '  const bool dense_initial_tiles=request_dense_tiles && dense_tiles_policy && tiled_initial &&\n')
    after[p] = replace(after[p], '    const bool dense_replay_tiles=request_dense_tiles && tiled_replay &&\n',
        '    const bool dense_replay_tiles=request_dense_tiles && dense_tiles_policy && tiled_replay &&\n')
    p = paths[3]
    after[p] = replace(after[p],
        '  ScopedEnvironment specialized("GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT");\n'
        '  setenv("GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT","1",1);\n',
        '  ScopedEnvironment specialized("GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT"), tiles("GAGP_GPU_TILED_INITIAL_PAYLOAD"),\n'
        '      dense("GAGP_GPU_DENSE_CASE_TILES"), view("GAGP_GPU_CASE_VIEW_BLOCK");\n'
        '  setenv("GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT","1",1);\n'
        '  setenv("GAGP_GPU_TILED_INITIAL_PAYLOAD","1",1);setenv("GAGP_GPU_DENSE_CASE_TILES","tuple",1);\n'
        '  setenv("GAGP_GPU_CASE_VIEW_BLOCK","hybrid",1);\n')
    after[p] = replace(after[p], '  return test_extended_payload() && test_dense_case_tiles();\n',
                       '  return test_extended_payload() && test_dense_case_tiles() && test_private_tuple_fitness();\n')
    p = paths[4]
    anchor = '          "case-sized view launch changed fractional scores, original logical lanes, fuel/errors or dirty workspace"))return false;\n    }\n'
    block = r"""
    {
      const auto* prior=std::getenv("GAGP_BOUND_ADD_PHASE");const bool present=prior;
      const std::string saved=prior?prior:"";setenv("GAGP_BOUND_ADD_PHASE","1",1);
      for(bool additive:{true,false})for(int count:{128,200}) {
        auto one=shape;
        if(!additive)one.bounded_region_segments[0].combine=constant_phase(Value::from_int(7));
        std::vector<CaseBindings> subset(cases.begin(),cases.begin()+count);
        std::vector<Value> expected(answers.begin(),answers.begin()+count);
        FitnessSessionGpu probe;
        if(!probe.init(subset,expected,fuel,512,7,policy).ok)return false;
        setenv("GAGP_GPU_CASE_VIEW_BLOCK","0",1);const auto ordinary=probe.eval_programs({one},true);
        setenv("GAGP_GPU_CASE_VIEW_BLOCK","hybrid",1);const auto hybrid=probe.eval_programs({one},true);
        const auto dirty=probe.eval_programs({one},true);setenv("GAGP_GPU_CASE_VIEW_BLOCK","0",1);
        std::vector<unsigned> cpu_wrong;
        const auto reference=eval_fitness_cpu({one},subset,expected,fuel,7,512,policy,&cpu_wrong);
        if(!check(ordinary.ok && hybrid.ok && dirty.ok && ordinary.fitness==reference &&
            hybrid.fitness==reference && dirty.fitness==reference && hybrid.wrong==cpu_wrong &&
            hybrid.case_counts==ordinary.case_counts && dirty.case_counts==hybrid.case_counts &&
            hybrid.physical_blocksize==(additive?512:128) &&
            (hybrid.execution_profile.find("-bound-add")!=std::string::npos)==additive,
            "hybrid physical view rule changed CPU scores, logical reductions, fuel/errors or compile-feature route"))return false;
      }
      if(present)setenv("GAGP_BOUND_ADD_PHASE",saved.c_str(),1);else unsetenv("GAGP_BOUND_ADD_PHASE");
    }
"""
    after[p] = replace(after[p], anchor, anchor + block)
    p = paths[5]
    anchor = '      if(!check(active==(std::string(enabled)=="1"),"private tuple opt-in admission"))return false;\n'
    block = r"""      const auto* dense_mode=std::getenv("GAGP_GPU_DENSE_CASE_TILES");
      const auto* tile_mode=std::getenv("GAGP_GPU_TILED_INITIAL_PAYLOAD");
      if(dense_mode && std::string(dense_mode)=="tuple" && tile_mode && std::string(tile_mode)=="1") {
        const bool dense_active=gpu.execution_profile.find("-dense-case-tiles")!=std::string::npos &&
            gpu.execution_profile.find("-dense-case-tiles-declined")==std::string::npos;
        if(!check(dense_active==active,"tuple-only dense tiles changed policy admission or fallback"))return false;
      }
"""
    after[p] = replace(after[p], anchor, anchor + block)
    target = serial.BASE / "native-private-layout5c.patch"
    assert not target.exists()
    target.write_text("".join("".join(difflib.unified_diff(before[p].splitlines(True), after[p].splitlines(True),
                                                        fromfile="a/" + p, tofile="b/" + p)) for p in paths))
    serial.save(serial.RAW / "private-layout5c-staged.json", dict(source_build="build-attempt4", patch=str(target),
                patch_sha256=serial.sha(target), changed_paths=paths,
                scope="New32-bound instantiation exposes already-proved private compact contiguous layout flags/null registry pointers; same VM checks/cases/fuel/workspace/RNG; adds tuple-only dense policy and compile-feature hybrid views, without task names; strict host guards and ordinary fallbacks"))


if __name__ == "__main__":
    main()
