"""Wait for complete global selection/TRAINING costs, freeze once, run formal."""
import os
from pathlib import Path
import time
import traceback

import freeze_and_run_shared
import serial

name='final-pipeline1';record=serial.RAW/(name+'-controller.json');assert not record.exists()
state=dict(pid=os.getpid(),status='waiting_for_selection_and_training_costs',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    harness_sha256={str(path):serial.sha(path) for path in [Path(__file__),serial.BASE/'freeze_and_run_shared.py',
        serial.BASE/'forecast_shared.py',serial.BASE/'formal_shared.py',serial.BASE/'full_population_audit_shared.py',
        serial.BASE/'full_population_audit_shared_timed.py',serial.BASE/'shared_cpu_reference.py']})
serial.save(record,state)


def exited(data):
    for key in ('pid','child_pid'):
        path=Path(f'/proc/{data.get(key)}/stat')
        if path.exists() and path.read_text().split()[2]!='Z':return False
    return True


try:
    prerequisite=[serial.RAW/(n+'-controller.json') for n in
        ('coarse-gate0','combined1','combined2','combined3','fused-cold-admission1','training13','training-timing8','preparation-timing6','shared-cpu-smoke0')]
    prerequisite += [serial.RAW/p/'manifest.json' for p in
        ('harness-checks-attempt19','reference-reader-smoke0','provenance-build22','resources-build22')]
    for path in prerequisite:
        while True:
            data=serial.read(path) if path.exists() else {}
            if data.get('status') in ('stopped','failed'):raise RuntimeError('formal prerequisite failed: '+str(path))
            if data.get('status') in ('ok','requested_phase_complete') and exited(data):break
            if time.time()>serial.DEADLINE-10800:raise RuntimeError('formal prerequisites missed original budget')
            time.sleep(10)
    # Already-started individual tails are preserved and must exit before
    # formal timing. Intentional unstarted cancellations remain explicit.
    for component in ('private-launch0','projection-simple0','view-geometry1','profile-shape19-0','profile-parent21-0','profile-cold19-0'):
        path=serial.RAW/(component+'-controller.json')
        while True:
            data=serial.read(path)
            if data['status'] in ('stopped','requested_phase_complete') and exited(data):break
            if time.time()>serial.DEADLINE-10800:raise RuntimeError('development tail missed original budget')
            time.sleep(10)
    for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
    state['status']='freezing_from_actual_training_costs';serial.save(record,state)
    freeze_and_run_shared.main('formal0','training13','training-timing8','preparation-timing6',
        ['combined1','combined2','combined3','stage1','shape1','threads0','repro-block0','parent-cache0','fused-cold0','coarse-gate0','profile-shape19-0','profile-parent21-0','profile-cold19-0'])
    result=serial.read(serial.RAW/'formal0-controller.json')
    state.update(status=result['status'],finished_epoch=time.time(),formal_controller=str(serial.RAW/'formal0-controller.json'))
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(record,state)
