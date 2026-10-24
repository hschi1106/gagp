"""Stage a compiler specialization using only host-proved private layout facts."""
import difflib

import serial
from stage_dense_candidate import replace


def main():
    paths = ["cpp/src/runtime/gpu/device/kernels.cuh", "cpp/src/runtime/gpu/fitness_gpu.cu",
             "cpp/tests/gpu/test_gpu_phase_population.cu", "cpp/tests/gpu/test_extended_payload_gpu.cu"]
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
        specialized.execution_profile.find("-private-layout-specialized-declined")==std::string::npos &&
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
    target = serial.BASE / "native-private-layout5.patch"
    assert not target.exists()
    target.write_text("".join("".join(difflib.unified_diff(before[p].splitlines(True), after[p].splitlines(True),
                                                        fromfile="a/" + p, tofile="b/" + p)) for p in paths))
    serial.save(serial.RAW / "private-layout5-staged.json", dict(source_build="build-attempt4", patch=str(target),
                patch_sha256=serial.sha(target), changed_paths=paths,
                scope="New32-bound instantiation exposes already-proved private compact contiguous layout flags/null registry pointers; same VM checks/cases/fuel/workspace/RNG, strict host guard and ordinary fallback"))


if __name__ == "__main__":
    main()
