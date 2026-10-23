"""Integrate the actually probed immutable-span operation; no semantic budget relaxation."""
import importlib.util
from pathlib import Path
import tempfile

ROOT=Path(__file__).resolve().parents[6]
BASE=Path(__file__).resolve().parent


def apply(dry_run=False):
    changed={}
    def edit(path,old,new,count=1):
        text=changed.get(path,(ROOT/path).read_text())
        assert text.count(old)==count,(path,old[:100],text.count(old))
        changed[path]=text.replace(old,new)
    # Run the EXACT passed overlay on a disposable one-header tree.
    spec=importlib.util.spec_from_file_location('passed_private_overlay',BASE/'private_append_overlay.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    path='cpp/src/runtime/gpu/device/builtins_device.cuh'
    with tempfile.TemporaryDirectory(prefix='gagp-adjacent-apply-') as directory:
        gpu=Path(directory);(gpu/'device').mkdir()
        (gpu/'device/builtins_device.cuh').write_text((ROOT/path).read_text())
        module.apply(gpu)
        changed[path]=(gpu/'device/builtins_device.cuh').read_text()
    edit('cpp/include/gagp/runtime/gpu/region_types_gpu.hpp',
         '  bool private_cell_soa = false;',
         '  bool private_cell_soa = false;\n  bool private_adjacent_concat = false;')
    for path,anchor in [('cpp/src/runtime/gpu/device/execute_bytecode_device.cuh',
                         '    payload_state.private_warp_copy=workspace.private_warp_copy;'),
                        ('cpp/src/runtime/gpu/device/kernels.cuh',
                         '            cache_payload_state.private_warp_copy=workspace.private_warp_copy;')]:
        prefix=anchor[:len(anchor)-len(anchor.lstrip())]
        state='cache_payload_state' if 'cache_payload' in anchor else 'payload_state'
        edit(path,anchor,anchor+'\n'+prefix+state+'.private_adjacent_concat=workspace.private_adjacent_concat;')
    path='cpp/src/runtime/gpu/fitness_gpu.cu'
    anchor='  region_workspace.private_cell_soa=request_cell_soa && region_workspace.compact_intlist_cells;'
    edit(path,anchor,anchor+'''
  const char* adjacent_setting=std::getenv("GAGP_GPU_PRIVATE_ADJACENT_CONCAT");
  const bool request_adjacent=adjacent_setting && std::string(adjacent_setting)=="1";
  // Whole-program private admission excludes memoized list states and public
  // handle escape. Keep the tested compact, contiguous per-lane backing only.
  region_workspace.private_adjacent_concat=request_adjacent &&
      region_workspace.compact_intlist_cells && !region_workspace.private_cell_soa;''')
    anchor='    if(request_cell_soa)out.execution_profile+=region_workspace.private_cell_soa?'
    edit(path,anchor,'''    if(request_adjacent)out.execution_profile+=region_workspace.private_adjacent_concat?
        "-private-adjacent-concat":"-private-adjacent-concat-declined";
'''+anchor)
    # Copy the independently checked CPU model and device trace unchanged.
    # Include it in its own namespace to avoid ordinary fixture identifiers.
    source=(BASE/'private_append_probe.cu').read_text()
    source=source[:source.index('\nint main() {')]
    split=source.index('\nusing namespace gagp;')
    includes,body=source[:split],source[split:]
    includes=includes.replace('runtime/gpu/device/builtins_device.cuh','../../src/runtime/gpu/device/builtins_device.cuh')
    test=(BASE/'runtime_private_adjacent_test_tail.cuh').read_text()
    target='cpp/tests/gpu/private_adjacent_checks.cuh'
    assert not (ROOT/target).exists()
    changed[target]='#pragma once\n'+includes+'\nnamespace private_adjacent_checks {\n#define PROBE_OPT\n'+body+test+'\n#undef PROBE_OPT\n}\n'
    path='cpp/tests/gpu/test_extended_payload_gpu.cu'
    edit(path,'#include "private_short_edit_checks.cuh"',
         '#include "private_short_edit_checks.cuh"\n#include "private_adjacent_checks.cuh"')
    edit(path,'  if(!test_private_short_edit_sessions())return 1;',
         '  if(!test_private_short_edit_sessions() || !private_adjacent_checks::run())return 1;')
    # Same owner, all32 generations: OFF vs ON exact fitness/wrong/counts;
    # the existing fixture also audits every individual on native CPU.
    path='cpp/tests/gpu/test_gpu_phase_population.cu'
    anchor='  const auto dirty=owner.evaluate(session,true);'
    edit(path,anchor,anchor+'''
  const char* adjacent_prior=std::getenv("GAGP_GPU_PRIVATE_ADJACENT_CONCAT");
  const bool adjacent_present=adjacent_prior!=nullptr;
  const std::string adjacent_saved=adjacent_prior?adjacent_prior:"";
  setenv("GAGP_GPU_PRIVATE_ADJACENT_CONCAT","0",1);
  const auto adjacent_off=owner.evaluate(session,true);
  setenv("GAGP_GPU_PRIVATE_ADJACENT_CONCAT","1",1);
  const auto adjacent_on=owner.evaluate(session,true);
  if(adjacent_present)setenv("GAGP_GPU_PRIVATE_ADJACENT_CONCAT",adjacent_saved.c_str(),1);
  else unsetenv("GAGP_GPU_PRIVATE_ADJACENT_CONCAT");
  require(adjacent_on.ok && adjacent_off.ok && adjacent_on.fitness==adjacent_off.fitness &&
      adjacent_on.wrong==adjacent_off.wrong && adjacent_on.case_counts==adjacent_off.case_counts &&
      adjacent_on.fitness==gpu.fitness && adjacent_on.wrong==gpu.wrong &&
      adjacent_on.execution_profile.find("-private-adjacent-concat")!=std::string::npos &&
      adjacent_on.execution_profile.find("-private-adjacent-concat-declined")==std::string::npos,
      "private adjacent backing changed full-case cross-generation results");''')
    if not dry_run:
        for path,text in changed.items():
            (ROOT/path).parent.mkdir(parents=True,exist_ok=True);(ROOT/path).write_text(text)
    return sorted(changed)
