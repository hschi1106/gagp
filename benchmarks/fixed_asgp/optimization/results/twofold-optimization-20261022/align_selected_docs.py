"""Keep maintained docs consistent with the exact promoted source, preserving other edits."""
from argparse import ArgumentParser
import fcntl
import json
from pathlib import Path
import shutil
import time
import traceback

import align_selected_source
import serial

ADJACENT='GAGP_GPU_PRIVATE_ADJACENT_CONCAT'
PAYLOAD='docs/design/payload.md'
NAV='.agents/skills/gagp-navigation/SKILL.md'
ADJACENT_PAYLOAD='''## Adjacent immutable private IntList storage

`GAGP_GPU_PRIVATE_ADJACENT_CONCAT=1` permits an admitted private compact,
contiguous IntList evaluator to join adjacent immutable spans by returning a
combined span. Interleaved compact cells decline this option. Type, encoded
length, logical allocation charges and original capacity/overflow checks still
run in their original order; the optimization never increases the admitted
list budget or removes a replay. No public payload identity changes.

The arena tracks ordinary reserved cells separately from physically written
cells. Singleton and non-adjacent concat append to physical storage. A generic
allocation synchronizes the physical cursor to the ordinary boundary. Returning
from a scalar region conservatively restarts at its saved ordinary boundary;
older live spans remain below that boundary. Handles are private to the proven
whole program; memoized IntList states are excluded by existing admission.

Ownership and operation checks live in `device/builtins_device.cuh`; the
workspace admission and profile marker are in `fitness_gpu.cu`. These paths are
relative to `cpp/src/runtime/gpu/`. `gagp_test_extended_payload_gpu` contains an
independent CPU-vector trace and an append/prepend positive witness retaining
all immutable prefixes with64 actual cells and2143 reserved/charged cells.
`gagp_test_gpu_phase_population` compares ON/OFF fitness, wrong counts and case
coverage throughout evolving private-list owners, alongside its CPU audits.
Actual test results and search speed remain separate study evidence.

'''
ADJACENT_NAV='''- Optional `GAGP_GPU_PRIVATE_ADJACENT_CONCAT`: immutable adjacent-span reuse
  and separate ordinary/physical arena cursors in
  `runtime/gpu/device/builtins_device.cuh`; compact/non-SoA admission in
  `runtime/gpu/fitness_gpu.cu`; state propagation in
  `device/execute_bytecode_device.cuh` and `device/kernels.cuh`. Contract:
  `docs/design/payload.md`. Tests: `gagp_test_extended_payload_gpu`,
  `gagp_test_gpu_phase_population`. Runtime paths are relative to `cpp/src/`.
'''


def rewrite(documents,available,blocks):
    result=dict(documents);removed=[]
    for block in blocks:
        path,text,flag=block['path'],block['text'],block['flag']
        if result[path].count(text)!=1:
            raise ValueError('study-owned doc block changed; inspect before mutation: '+flag+'/'+path)
        if flag not in available:
            result[path]=result[path].replace(text,'',1);removed.append((flag,path))
    if ADJACENT in available:
        if ADJACENT in result[PAYLOAD] or ADJACENT in result[NAV]:
            raise ValueError('adjacent documentation already exists; preserve and inspect it')
        # Insert after the original introduction, without replacing its prose.
        anchor='This document explains how C++ runtime container values work under `cpp/` and how the exact and fallback paths interact.\n\n'
        if result[PAYLOAD].count(anchor)!=1:raise ValueError('payload introduction changed')
        result[PAYLOAD]=result[PAYLOAD].replace(anchor,anchor+ADJACENT_PAYLOAD,1)
        anchor='- Resident payload scoring is shared by the VM'
        if result[NAV].count(anchor)!=1:raise ValueError('navigation insertion point changed')
        result[NAV]=result[NAV].replace(anchor,ADJACENT_NAV+anchor,1)
    return result,removed


def main(alignment_path,name,execute=False):
    out=serial.RAW/name;out.mkdir()
    state=dict(status='checking',started_epoch=time.time(),source_alignment=str(alignment_path),execute=execute)
    record=out/'manifest.json';serial.save(record,state)
    try:
        with serial.GPU_LOCK.open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX)
            aligned=serial.read(alignment_path)
            if aligned.get('status')!='ok' or not aligned.get('execute'):raise ValueError('exact selected source is not promoted')
            current=align_selected_source.file_map(align_selected_source.source_paths())
            if current!=aligned['selected_source_sha256']:raise ValueError('promoted C++ source changed')
            blocks_path=serial.BASE/'maintained-doc-blocks.json';blocks=serial.read(blocks_path)
            paths={b['path'] for b in blocks}|{PAYLOAD,NAV}
            if any((serial.ROOT/p).is_symlink() for p in paths):raise ValueError('documentation is a symlink')
            documents={p:(serial.ROOT/p).read_text() for p in paths}
            flags={b['flag'] for b in blocks}|{ADJACENT};available=set()
            for path in current:
                if path.startswith('cpp/src/'):
                    source=(serial.ROOT/path).read_text()
                    available|={flag for flag in flags if flag in source}
            proposed,removed=rewrite(documents,available,blocks)
            changed=sorted(p for p in paths if proposed[p]!=documents[p])
            state.update(available_flags=sorted(available),removed_blocks=removed,changed_paths=changed,
                source_alignment_sha256=serial.sha(alignment_path),blocks_sha256=serial.sha(blocks_path),
                before_sha256={p:serial.sha(serial.ROOT/p) for p in paths})
            serial.save(record,state)
            if execute:
                for p in paths:
                    backup=out/'previous-docs'/p;backup.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(serial.ROOT/p,backup)
                for p in changed:
                    if serial.sha(serial.ROOT/p)!=state['before_sha256'][p]:raise ValueError('documentation changed during planning')
                    (serial.ROOT/p).write_text(proposed[p])
            state.update(status='ok',finished_epoch=time.time(),after_sha256={p:serial.sha(serial.ROOT/p) for p in paths},
                scope='exact captured study-owned optional-feature sections only; all other text retained; not a test or speed claim')
    except BaseException as error:
        state.update(status='failed',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
    finally:serial.save(record,state)


if __name__=='__main__':
    parser=ArgumentParser(description=__doc__)
    parser.add_argument('--alignment',type=Path,required=True);parser.add_argument('--name',required=True)
    parser.add_argument('--execute',action='store_true');args=parser.parse_args();main(args.alignment,args.name,args.execute)
