"""Read-only, stronger decode-context verification of five OLD TRAINING audits."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import full_population_audit
import reference_reader
import report_data
import serial

name='reference-reader-smoke0';out=serial.RAW/name;out.mkdir();record=out/'manifest.json'
state=dict(pid=os.getpid(),status='waiting_for_shared_smoke',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    scope='read-only oldTRAINING artifact identity/CPU-reference proof; zero new GPU/CPU program executions')
serial.save(record,state)
try:
    while True:
        smoke=serial.read(serial.RAW/'shared-cpu-smoke0-controller.json')
        if smoke['status']=='stopped':raise RuntimeError('native CPU-reference smoke stopped')
        if smoke['status']=='ok':
            proc=Path(f'/proc/{smoke["pid"]}/stat')
            if not proc.exists() or proc.read_text().split()[2]=='Z':break
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('reference reader prerequisite missed budget')
        time.sleep(10)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        paths=[Path(__file__),serial.BASE/'reference_reader.py',serial.BASE/'shared_cpu_reference.py',serial.BASE/'report_data.py']
        state.update(status='checking_artifacts',source_sha256={str(path):serial.sha(path) for path in paths});serial.save(record,state)
        protocol=serial.read(serial.BASE/'protocol.json');evidence=report_data.Evidence();rows=[]
        native=serial.read(serial.RAW/'shared-cpu-smoke0-candidate/manifest.json')
        old=serial.read(serial.RAW/'full-population-audit-smoke0/manifest.json')
        fresh=serial.read(serial.RAW/'shared-cpu-smoke0-baseline-reference/manifest.json')
        small=serial.read(serial.RAW/'simple1-p1024-baseline/manifest.json')
        large=serial.read(serial.RAW/'stage1-p16384-baseline/manifest.json')
        for job in native['jobs'].values():
            assert job['status']=='ok'
            task=job['task'];population=job['population']
            origin=old['jobs'][f'{task}.p1024.candidate.final.full-native'] if population==1024 else fresh['jobs'][f'{task}.p16384.baseline.final.full-native']
            reconstructed=reference_reader.reconstruct(job,evidence,protocol['settings'],origin)
            assert reconstructed==evidence.output(job)
            original=(small if population==1024 else large)['jobs'][f'{task}.p{population}.s0.g8']
            checks=full_population_audit.validate(reconstructed,evidence.output(original),population,1024)
            assert checks['native_fidelity_pass'] and all(job[key]==value for key,value in checks.items())
            rows.append(dict(task=task,population=population,cpu_reference_programs=job['native_cpu_reference_programs'],
                candidate_cpu_executions=job['native_cpu_executed_programs'],mismatches=checks['native_mismatches']))
        assert len(rows)==5 and sum(row['population'] for row in rows)==20480
        state.update(status='ok',rows=rows,new_program_executions=0,artifact_sha256=evidence.hashes,finished_epoch=time.time())
except BaseException as error:
    state.update(status='failed',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(record,state)
