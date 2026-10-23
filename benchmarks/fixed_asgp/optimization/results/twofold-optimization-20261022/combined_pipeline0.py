"""Compose the independent global TRAINING winners and measure full30 afresh."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import combined_pilot
import serial
import view_geometry_pool_pilot

name='combined0';record=serial.RAW/(name+'-controller.json');assert not record.exists()
paths=[Path(__file__),serial.BASE/'combined_pilot.py',serial.BASE/'stage_pilot.py',serial.BASE/'vm_pilot.py',
    serial.BASE/'view_geometry_pool_pilot.py',serial.BASE/'serial.py',serial.BASE/'protocol.json']
state=dict(pid=os.getpid(),status='waiting_for_independent_development',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    harness_sha256={str(p):serial.sha(p) for p in paths},
    rule='compose one global geometry/pool, one private bound, one dependency/simple choice from oldTRAINING; no per-task settings or heldout; full30 bothP fresh controls')
serial.save(record,state)
try:
    for prior in ('view-geometry1','private-launch0','projection-simple0'):
        while True:
            previous=serial.read(serial.RAW/(prior+'-controller.json'))
            if previous['status']=='stopped':raise RuntimeError('component development stopped; inspect failure: '+prior)
            if previous['status']=='requested_phase_complete':
                proc=Path(f'/proc/{previous["pid"]}/stat')
                if not proc.exists() or proc.read_text().split()[2]=='Z':break
            if time.time()>serial.DEADLINE-10800:raise RuntimeError('combined development prerequisite missed budget')
            time.sleep(10)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
        geometry=serial.read(serial.RAW/'view-geometry1-controller.json')['selection']
        private=serial.read(serial.RAW/'private-launch0-controller.json')['selection']
        projected=serial.read(serial.RAW/'projection-simple0-controller.json')['selection']
        extra=view_geometry_pool_pilot.execution_settings(geometry['selected'],pool_mode=geometry['pool_mode'])
        bound=private['extra']['GAGP_GPU_PRIVATE_LAUNCH_BOUND'];assert bound in ('0','512','1024')
        extra['GAGP_GPU_PRIVATE_LAUNCH_BOUND']=bound
        for key in ('GAGP_GPU_PROJECT_SHAPE_INPUTS','GAGP_GPU_SIMPLE_PHASES'):extra[key]=projected['extra'][key]
        build=serial.read(serial.RAW/'build-attempt20/manifest.json')
        assert build['status']=='ok' and serial.sha(build['binary'])==build['binary_sha256']
        settings=serial.RAW/(name+'-settings.json');assert not settings.exists()
        chosen=dict(extra=extra,geometry=geometry,private=private,projection=projected,
            binary=build['binary'],binary_sha256=build['binary_sha256'],epoch=time.time(),
            scope='global composed oldTRAINING setting; not formal/quality freeze')
        serial.save(settings,chosen)
        state.update(status='all30_p1024',settings=str(settings),settings_sha256=serial.sha(settings),selection=chosen)
        serial.save(record,state)
        protocol=serial.read(serial.BASE/'protocol.json')
    for population in (1024,16384):
        state['status']=f'all30_p{population}';serial.save(record,state)
        combined_pilot.run(build['binary'],f'{name}-p{population}',[population],protocol['tasks'],'candidate',pool_mode=geometry['pool_mode'])
    state.update(status='requested_phase_complete',finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(record,state)
