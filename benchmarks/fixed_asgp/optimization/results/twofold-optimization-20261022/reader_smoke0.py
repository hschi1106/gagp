"""Read-only report parser check against admitted TRAINING audit artifacts."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import champion_audit_dedup
import full_population_audit
import report_data
import serial

out=serial.RAW/'report-reader-smoke0';out.mkdir()
state=dict(pid=os.getpid(),status='waiting_for_lock',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
           scope='read-only parsing of four existing TRAINING audits; no new search, CPU execution or heldout')
serial.save(out/'manifest.json',state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        state.update(status='running',source_sha256={str(p):serial.sha(p) for p in [Path(__file__),
            serial.BASE/'report_data.py',serial.BASE/'champion_audit_dedup.py',serial.BASE/'full_population_audit.py']})
        serial.save(out/'manifest.json',state)
        previous=serial.read(serial.RAW/'full-population-audit-smoke0/manifest.json')
        search=serial.read(serial.RAW/'simple1-p1024-baseline/manifest.json');evidence=report_data.Evidence();rows=[]
        for job in previous['jobs'].values():
            assert job['status']=='ok'
            doc=evidence.output(job);raw=evidence.output(job,'raw_output');mapping=evidence.output(job,'mapping')
            expanded=champion_audit_dedup.expand_document(raw,mapping['observation_to_unique'])
            assert expanded==doc
            old=search['jobs'][job['task']+'.p1024.s0.g8'];original=evidence.output(old)
            checks=full_population_audit.validate(doc,original,1024,1024)
            assert checks['native_fidelity_pass'] and all(job[k]==v for k,v in checks.items())
            rows.append(dict(task=job['task'],verified_logical_members=1024,
                             recorded_cpu_unique_programs=job['native_cpu_executed_programs'],mismatches=checks['native_mismatches']))
        assert len(rows)==4
        state.update(status='ok',rows=rows,artifact_sha256=evidence.hashes,finished_epoch=time.time())
except BaseException as error:
    state.update(status='failed',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(out/'manifest.json',state)
