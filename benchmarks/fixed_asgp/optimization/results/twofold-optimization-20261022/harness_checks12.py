"""Check current study contracts and syntax under the shared execution lock."""
import fcntl
import os
import subprocess
import time
import traceback

import serial

out=serial.RAW/'harness-checks-attempt12';out.mkdir()
state=dict(status='waiting',pid=os.getpid(),started_epoch=time.time(),deadline_epoch=serial.DEADLINE)
serial.save(out/'manifest.json',state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        helpers=['bottlenecks.py','bottleneck_analysis0.py','projection_simple_pilot.py','projection_simple_pipeline0.py','best_selection_private20.py','training_best_private20.py','training_timing5.py','preparation_timing3.py','forecast.py','freeze_and_run.py','report.py','report_views.py','report_validation.py','private_launch_pilot.py','private_launch_pipeline0.py','private_launch_candidate0.py','runtime_forecast.py','freeze_rule.py','quality_dedup.py','champion_audit_dedup.py',
                 'full_population_audit.py','full_population_audit_v2.py','report_data.py','report_qualification.py','formal_dedup.py','preparation_timing0.py',
                 'stage_pilot.py','view_geometry_pool_pilot.py','view_geometry_pipeline1.py','best_selection_geometry1.py',
                 'training_best_geometry1.py','training_timing4.py','preparation_timing2.py','quality_metrics.py','serial.py','quality_data.py']
        checked=list(serial.BASE.glob('test_*.py'))+[serial.BASE/name for name in helpers]
        state.update(status='running',source_sha256={str(p):serial.sha(p) for p in checked});serial.save(out/'manifest.json',state)
        for path in checked:compile(path.read_text(),str(path),'exec')
        command=['python3','-m','unittest','discover','-s',str(serial.BASE),'-p','test_*.py','-v']
        with (out/'checks.log').open('x') as log:
            child=subprocess.Popen(command,cwd=serial.ROOT,stdout=log,stderr=subprocess.STDOUT)
            state.update(child_pid=child.pid,command=command);serial.save(out/'manifest.json',state)
            result=child.wait()
        assert all(serial.sha(p)==h for p,h in state['source_sha256'].items()),'sources changed during checks'
        state.update(status='ok' if result==0 else 'failed',returncode=result,finished_epoch=time.time())
except BaseException as error:
    state.update(status='failed',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(out/'manifest.json',state)
