"""Add fused admission only after its fixed coarse rule admits it, then full30."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import best_selection_extra_recipe
import combined3_pilot
import serial

name='combined3';record=serial.RAW/(name+'-controller.json');assert not record.exists()
paths=[Path(__file__),serial.BASE/'combined3_pilot.py',serial.BASE/'best_selection_extra_recipe.py',
       serial.BASE/'best_selection_coarse_recipe.py',serial.BASE/'stage_pilot.py',
       serial.BASE/'serial.py',serial.BASE/'protocol.json']
state=dict(pid=os.getpid(),status='waiting_for_full30_and_fused_coarse',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
           harness_sha256={str(p):serial.sha(p) for p in paths},
           rule='fixed fused-coarse >=2% rule; compose with best COMPLETE60-cell global recipe; '
                'new all30/bothP/G8/fresh731 controls; full AST/score/champion fidelity; old TRAINING only')
serial.save(record,state)
try:
    controls={}
    for component in ('fused-cold0','combined2'):
        while True:
            d=serial.read(serial.RAW/(component+'-controller.json'))
            if d['status']=='stopped':raise RuntimeError('prerequisite stopped; inspect preserved cause: '+component)
            proc=Path(f'/proc/{d["pid"]}/stat')
            if d['status']=='requested_phase_complete' and (not proc.exists() or proc.read_text().split()[2]=='Z'):
                controls[component]=d;break
            if time.time()>serial.DEADLINE-10800:raise RuntimeError('combined3 prerequisite missed original budget')
            time.sleep(10)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
        path=serial.RAW/'fused-cold0-development-selection.json';fused=serial.read(path)
        comparisons=serial.RAW/'fused-cold0-coarse-comparisons.json'
        assert serial.sha(comparisons)==fused['comparisons_sha256']
        evidence={str(path):serial.sha(path),str(comparisons):serial.sha(comparisons)}
        if not fused['enabled']:
            state.update(status='requested_phase_complete',run_full=False,
                         reason='fused admission did not pass fixed coarse benefit rule',
                         evidence_sha256=evidence,finished_epoch=time.time());serial.save(record,state)
        else:
            prior=best_selection_extra_recipe.select();base=prior['selected']
            build=serial.read(serial.RAW/'build-attempt22/manifest.json')
            admitted=serial.read(serial.RAW/'fused-cold-admission1-controller.json')
            assert build['status']==admitted['status']=='ok'
            assert serial.sha(build['binary'])==admitted['binary_sha256']==build['binary_sha256']
            extra=dict(base['extra'],GAGP_FUSED_COLD_EXECUTABLE='1')
            chosen=dict(extra=extra,binary=build['binary'],binary_sha256=build['binary_sha256'],
                        build_name='build-attempt22',base_selection=prior,fused_selection=fused,
                        evidence_sha256=evidence,epoch=time.time(),scope=state['rule'])
            settings=serial.RAW/'combined3-settings.json';assert not settings.exists();serial.save(settings,chosen)
            state.update(status='all30_p1024',run_full=True,selection=chosen,
                         settings=str(settings),settings_sha256=serial.sha(settings));serial.save(record,state)
            protocol=serial.read(serial.BASE/'protocol.json')
    if state['run_full']:
        for population in (1024,16384):
            state['status']=f'all30_p{population}';serial.save(record,state)
            combined3_pilot.run(build['binary'],f'{name}-p{population}',[population],protocol['tasks'],
                                'candidate',pool_mode=int(extra['GAGP_GPU_CUDA_POOL']))
        state.update(status='requested_phase_complete',finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(record,state)
