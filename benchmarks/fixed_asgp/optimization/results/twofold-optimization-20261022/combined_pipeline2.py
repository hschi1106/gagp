"""Complete full30 validation only if new global block/cache coarse wins."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import combined2_pilot
import serial

name='combined2';record=serial.RAW/(name+'-controller.json');assert not record.exists()
paths=[Path(__file__),serial.BASE/'combined2_pilot.py',serial.BASE/'stage_pilot.py',
       serial.BASE/'vm_pilot.py',serial.BASE/'serial.py',serial.BASE/'protocol.json']
state=dict(pid=os.getpid(),status='waiting_for_complete_development',started_epoch=time.time(),
           deadline_epoch=serial.DEADLINE,harness_sha256={str(p):serial.sha(p) for p in paths},
           rule='compose existing global combined1 recipe with block/cache only if fixed '
                'coarse >=2% rules admit; then new full30/bothP controls and exact fidelity; '
                'all components use old TRAINING; no heldout')
serial.save(record,state)
try:
    controls={}
    for component in ('repro-block0','parent-cache0','combined1'):
        while True:
            p=serial.RAW/(component+'-controller.json')
            d=serial.read(p) if p.exists() else {}
            if d.get('status')=='stopped':raise RuntimeError('component stopped: '+component)
            proc=Path(f'/proc/{d.get("pid")}/stat')
            if d.get('status')=='requested_phase_complete' and (
                    not proc.exists() or proc.read_text().split()[2]=='Z'):
                controls[component]=d;break
            if time.time()>serial.DEADLINE-10800:raise RuntimeError('combined2 prerequisite missed budget')
            time.sleep(10)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
        evidence={}
        choices={}
        for component in ('repro-block0','parent-cache0'):
            p=serial.RAW/(component+'-development-selection.json')
            choices[component]=serial.read(p);evidence[str(p)]=serial.sha(p)
            rows=serial.RAW/(component+'-coarse-comparisons.json')
            assert serial.sha(rows)==choices[component].get('coarse_comparisons_sha256',
                                                           choices[component].get('comparisons_sha256'))
            evidence[str(rows)]=serial.sha(rows)
        block=choices['repro-block0']['threads'];cache=choices['parent-cache0']['enabled']
        assert block in (1,2,4,8,16,32,64,128) and isinstance(cache,bool)
        protocol=serial.read(serial.BASE/'protocol.json')
        if block==1 and not cache:
            state.update(status='requested_phase_complete',run_full=False,
                         reason='new block/cache rejected by frozen coarse rules',
                         evidence_sha256=evidence,finished_epoch=time.time());serial.save(record,state)
        else:
            build_name='build-attempt21' if cache else 'build-attempt20'
            build=serial.read(serial.RAW/build_name/'manifest.json')
            assert build['status']=='ok' and serial.sha(build['binary'])==build['binary_sha256']
            original=serial.RAW/'combined1-settings.json'
            assert serial.sha(original)==controls['combined1']['settings_sha256']
            evidence[str(original)]=serial.sha(original)
            extra=dict(serial.read(original)['extra'],GAGP_PHASE_REPRO_THREADS=str(block),
                       GAGP_GPU_PARENT_FITNESS_CACHE='1' if cache else '0')
            chosen=dict(extra=extra,build_name=build_name,binary=build['binary'],
                        binary_sha256=build['binary_sha256'],epoch=time.time(),
                        block_selection=choices['repro-block0'],cache_selection=choices['parent-cache0'],
                        evidence_sha256=evidence,scope=state['rule'])
            settings=serial.RAW/(name+'-settings.json');assert not settings.exists();serial.save(settings,chosen)
            state.update(status='all30_p1024',run_full=True,selection=chosen,settings=str(settings),
                         settings_sha256=serial.sha(settings));serial.save(record,state)
    if state['run_full']:
        for population in (1024,16384):
            state['status']=f'all30_p{population}';serial.save(record,state)
            combined2_pilot.run(build['binary'],f'{name}-p{population}',[population],
                                protocol['tasks'],'candidate',pool_mode=int(extra['GAGP_GPU_CUDA_POOL']))
        state.update(status='requested_phase_complete',finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(record,state)
