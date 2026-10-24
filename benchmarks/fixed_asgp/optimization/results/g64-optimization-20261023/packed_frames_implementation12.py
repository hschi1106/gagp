"""Create an isolated opt-in packed-frame implementation; never edit formal0 CPP."""
from pathlib import Path
import difflib
import json
import shutil
import subprocess
import time

import serial


def create(name):
    attempt = serial.RAW / name
    attempt.mkdir()
    root = attempt / "source"
    root.mkdir()
    paths = subprocess.check_output(["git", "ls-files", "cpp"], cwd=serial.ROOT, text=True).splitlines()
    originals = {}
    for relative in paths:
        source = serial.ROOT / relative
        target = root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        originals[relative] = source.read_bytes()
    for name in ("spec", "configs", "data", "docs", "tools", "tests", "ref", "VERSION.md", "AGENTS.md"):
        source = serial.ROOT / name
        if source.exists():
            (root / name).symlink_to(source, target_is_directory=source.is_dir())

    def edit(relative, before, after, count=1):
        path = root / relative
        source = path.read_text()
        assert source.count(before) == count, (relative, before, source.count(before))
        path.write_text(source.replace(before, after))

    include = root / "cpp/include/gagp/runtime/gpu"
    for name in ("private_window_frames_gpu.hpp", "private_window_frame_views.hpp"):
        shutil.copy2(serial.BASE / "prototype-private-frames" / name, include / name)
    edit("cpp/src/runtime/gpu/device/execute_bytecode_device.cuh",
         '#include "../dense_coordinate_memo.hpp"',
         '#include "../dense_coordinate_memo.hpp"\n#include "gagp/runtime/gpu/private_window_frame_views.hpp"')
    edit("cpp/include/gagp/runtime/gpu/region_types_gpu.hpp",
         "  bool unboxed_window_frames = false;",
         "  bool packed_private_window_frames = false;\n  bool packed_private_window_soa = false;\n  bool unboxed_window_frames = false;")
    specialization = '''template<unsigned States, bool SoA>
struct DWindowFrameAccess<PackedPrivateWindowLocalFrame<States>, SoA> {
  DRegionFrame* base; std::size_t stride;
  __device__ InitializingPackedWindowFrameView<States> operator[](std::size_t index) const {
    if constexpr (SoA)
      return initializing_packed_soa_view<States>(reinterpret_cast<std::int64_t*>(base), index, stride);
    else
      return initializing_packed_aos_view(reinterpret_cast<PackedPrivateWindowFrameStorage<States>*>(base)[index]);
  }
};

'''
    edit("cpp/src/runtime/gpu/device/window_execution_device.cuh", "// Capability: IntList state", specialization + "// Capability: IntList state")
    dispatch = '''      if(workspace.packed_private_window_frames) {
        if(workspace.packed_private_window_soa) {
          if(workspace.two_state_frames)return d_run_window_region<PackedPrivateWindowLocalFrame<2>,true,Flavor,ValueTag::IntList>(
              segment,operands,caller_locals,caller_set,payload_tables,payload_state,tables,fuel,workspace);
          return d_run_window_region<PackedPrivateWindowLocalFrame<1>,true,Flavor,ValueTag::IntList>(
              segment,operands,caller_locals,caller_set,payload_tables,payload_state,tables,fuel,workspace);
        }
        if(workspace.two_state_frames)return d_run_window_region<PackedPrivateWindowLocalFrame<2>,false,Flavor,ValueTag::IntList>(
            segment,operands,caller_locals,caller_set,payload_tables,payload_state,tables,fuel,workspace);
        return d_run_window_region<PackedPrivateWindowLocalFrame<1>,false,Flavor,ValueTag::IntList>(
            segment,operands,caller_locals,caller_set,payload_tables,payload_state,tables,fuel,workspace);
      }
'''
    edit("cpp/src/runtime/gpu/device/region_execution_device.cuh",
         "    if(workspace.window_executor && payload_state.private_intlists) {\n",
         "    if(workspace.window_executor && payload_state.private_intlists) {\n" + dispatch)
    offset = '''      } else if(workspace.packed_private_window_frames) {
        if(workspace.packed_private_window_soa) {
          const auto words=workspace.two_state_frames?6:5;
          workspace.frames=reinterpret_cast<DRegionFrame*>(reinterpret_cast<std::int64_t*>(workspace.frames)+
              block_base*workspace.frame_capacity*words+tid);
        } else workspace.frames=reinterpret_cast<DRegionFrame*>(workspace.two_state_frames?
            reinterpret_cast<void*>(reinterpret_cast<PackedPrivateWindowFrameStorage<2>*>(workspace.frames)+slot):
            reinterpret_cast<void*>(reinterpret_cast<PackedPrivateWindowFrameStorage<1>*>(workspace.frames)+slot));
'''
    edit("cpp/src/runtime/gpu/device/kernels.cuh", "      } else if(workspace.unboxed_window_frames) {", offset + "      } else if(workspace.unboxed_window_frames) {")
    edit("cpp/src/runtime/gpu/fitness_gpu.cu",
         "    workspace.unboxed_window_frames=false;workspace.window_soa=false;",
         "    workspace.unboxed_window_frames=false;workspace.window_soa=false;\n"
         "    workspace.packed_private_window_frames=workspace.window_executor && workspace.packed_private_window_frames;\n"
         "    workspace.packed_private_window_soa=workspace.packed_private_window_frames && workspace.packed_private_window_soa;")
    edit("cpp/src/runtime/gpu/fitness_gpu.cu",
         "    const auto frame_size=!workspace.compact_frames?sizeof(gpu_detail::DRegionFrame):",
         "    const auto frame_size=workspace.packed_private_window_frames?\n"
         "        (workspace.two_state_frames?sizeof(gpu_detail::PackedPrivateWindowFrameStorage<2>):sizeof(gpu_detail::PackedPrivateWindowFrameStorage<1>)):\n"
         "        !workspace.compact_frames?sizeof(gpu_detail::DRegionFrame):")
    admission = '''  const auto* packed_window_setting=std::getenv("GAGP_GPU_PACKED_PRIVATE_FRAMES");
  const bool request_packed_window=packed_window_setting &&
      (std::string(packed_window_setting)=="aos" || std::string(packed_window_setting)=="soa");
  region_workspace.packed_private_window_frames=request_packed_window && private_intlists &&
      region_workspace.window_executor && region_workspace.compact_frames;
  region_workspace.packed_private_window_soa=region_workspace.packed_private_window_frames &&
      std::string(packed_window_setting)=="soa";
'''
    edit("cpp/src/runtime/gpu/fitness_gpu.cu", "  region_workspace.coordinate_executor = direct_phases", admission + "  region_workspace.coordinate_executor = direct_phases")
    edit("cpp/src/runtime/gpu/fitness_gpu.cu",
         "  std::size_t frame_size = region_workspace.coordinate_executor ?",
         "  std::size_t frame_size = region_workspace.packed_private_window_frames?\n"
         "      (region_workspace.two_state_frames?sizeof(gpu_detail::PackedPrivateWindowFrameStorage<2>):sizeof(gpu_detail::PackedPrivateWindowFrameStorage<1>)):\n"
         "      region_workspace.coordinate_executor ?")
    edit("cpp/src/runtime/gpu/fitness_gpu.cu", "      generic_workspace.window_executor=false;",
         "      generic_workspace.packed_private_window_frames=false;generic_workspace.packed_private_window_soa=false;\n      generic_workspace.window_executor=false;")
    edit("cpp/src/runtime/gpu/fitness_gpu.cu", "    if (region_workspace.unboxed_window_frames) out.execution_profile += \"-unboxed\";",
         "    if(request_packed_window)out.execution_profile+=region_workspace.packed_private_window_frames?\n"
         "        (region_workspace.packed_private_window_soa?\"-packed-private-window-soa\":\"-packed-private-window-aos\"):\"-packed-private-window-declined\";\n"
         "    if (region_workspace.unboxed_window_frames) out.execution_profile += \"-unboxed\";")

    check = '''  {
   const char* flag="GAGP_GPU_PACKED_PRIVATE_FRAMES";const auto* prior=std::getenv(flag);
   const bool present=prior;const std::string saved=prior?prior:"";
   setenv(flag,"0",1);const auto ordinary_frames=owner.evaluate(dense_session,true);
   for(const char* mode:{"aos","soa"}) {
    setenv(flag,mode,1);const auto packed=owner.evaluate(dense_session,true);
    const auto dirty=owner.evaluate(dense_session,true);
    std::vector<unsigned> expected_wrong;
    const auto expected=eval_fitness_cpu(independent,dense_cases,dense_answers,audit_fuel,1000,512,policy,&expected_wrong);
    require(ordinary_frames.ok && packed.ok && dirty.ok && packed.fitness==expected &&
        ordinary_frames.fitness==packed.fitness && dirty.fitness==packed.fitness && packed.wrong==expected_wrong &&
        dirty.wrong==packed.wrong && ordinary_frames.case_counts==packed.case_counts && dirty.case_counts==packed.case_counts &&
        (packed.execution_profile.find("-packed-private-window-declined")==std::string::npos)==sequence_windows,
        "packed private frames changed fresh-generation CPU scores, fuel/errors, dirty reuse or fallback");
   }
   if(present)setenv(flag,saved.c_str(),1);else unsetenv(flag);
  }
'''
    edit("cpp/tests/gpu/test_gpu_phase_population.cu", "  for(auto value:wrong)saw_content_match|=value<1024;", check + "  for(auto value:wrong)saw_content_match|=value<1024;")
    # Exercise private replay/fuel/mixed-public fallback helpers under each mode.
    check = '''bool test_packed_private_frames() {
  ScopedEnvironment packed("GAGP_GPU_PACKED_PRIVATE_FRAMES");
  for(const char* mode:{"aos","soa"}) {
    setenv("GAGP_GPU_PACKED_PRIVATE_FRAMES",mode,1);
    if(!test_private_layout_fallbacks() || !test_initial_payload_capacity_replay())return false;
  }
  return true;
}

'''
    edit("cpp/tests/gpu/test_extended_payload_gpu.cu", "bool test_auto_initial_payload_inputs() {", check + "bool test_auto_initial_payload_inputs() {")
    edit("cpp/tests/gpu/test_extended_payload_gpu.cu", "  if (!test_private_layout_fallbacks() ||", "  if (!test_packed_private_frames() || !test_private_layout_fallbacks() ||")
    fixture_relative = "cpp/tests/fixtures/phase_layout_measure_intlist_pair_packed.json"
    fixture = json.loads((root / "cpp/tests/fixtures/phase_layout_measure_intlist.json").read_text())
    structured = fixture["templates"][0]["body"]["structured"]
    structured["plan"]["state_types"].append("Int")
    for request in structured["plan"]["requests"]:
        request["states"].append(dict(kind="copy_state", source_state=1, offset="0", expression=0))
    fixture["templates"][0]["body"]["args"].insert(1, {"constant": {"type": "Int", "values": ["9007199254741001"]}})
    for phase in fixture["templates"][0]["body"]["phases"]:
        phase["argument"] += 1
    (root / fixture_relative).write_text(json.dumps(fixture, indent=2) + "\n")
    edit("cpp/tests/gpu/test_gpu_phase_population.cu",
         "void test_private_intlist_generations(bool sequence_windows=false,bool staged=false,bool edit_distance=false) {",
         "void test_private_intlist_generations(bool sequence_windows=false,bool staged=false,bool edit_distance=false,bool carried_state=false) {")
    edit("cpp/tests/gpu/test_gpu_phase_population.cu",
         'grammar::load_definition(sequence_windows?GAGP_REPOSITORY_ROOT "/cpp/tests/fixtures/phase_layout_measure_intlist.json":',
         'grammar::load_definition(carried_state?GAGP_REPOSITORY_ROOT "/cpp/tests/fixtures/phase_layout_measure_intlist_pair_packed.json":sequence_windows?GAGP_REPOSITORY_ROOT "/cpp/tests/fixtures/phase_layout_measure_intlist.json":')
    edit("cpp/tests/gpu/test_gpu_phase_population.cu", " test_private_intlist_generations(true,true);",
         " test_private_intlist_generations(true,true);\n test_private_intlist_generations(true,true,false,true);")
    changed = {}
    patches = []
    for relative, before in originals.items():
        after = (root / relative).read_bytes()
        if after != before:
            changed[relative] = serial.sha(root / relative)
            patches.extend(difflib.unified_diff(before.decode().splitlines(True), after.decode().splitlines(True),
                                                fromfile="a/" + relative, tofile="b/" + relative))
    for relative in ("cpp/include/gagp/runtime/gpu/private_window_frames_gpu.hpp",
                     "cpp/include/gagp/runtime/gpu/private_window_frame_views.hpp", fixture_relative):
        changed[relative] = serial.sha(root / relative)
        patches.extend(difflib.unified_diff([], (root / relative).read_text().splitlines(True),
                                            fromfile="/dev/null", tofile="b/" + relative))
    (attempt / "implementation.patch").write_text("".join(patches))
    record = dict(status="isolated_source_ready", created_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                  original_commit=subprocess.check_output(["git", "rev-parse", "HEAD"],cwd=serial.ROOT,text=True).strip(),
                  root=str(root), original_source_sha256={p:serial.sha(serial.ROOT/p) for p in paths},
                  source_sha256={str(p.relative_to(root)):serial.sha(p) for p in (root/"cpp").rglob("*") if p.is_file()},
                  changed_source_sha256=changed, implementation_patch_sha256=serial.sha(attempt/"implementation.patch"),
                  scope="Post-delivery old-TRAINING development only; formal0 source/binary/settings/results unchanged. No fresh heldout used.")
    serial.save(attempt / "source-manifest.json", record)
    return attempt, record


if __name__ == "__main__":
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name",required=True)
    args=parser.parse_args()
    create(args.name)
