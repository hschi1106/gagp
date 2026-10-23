"""Exercise the full speed reader on labelled OLD TRAINING parser fixtures.

Search aliases below are test inputs, not fresh comparisons. Only four existing
native audits are reused for parser validation; no new CPU execution is claimed.
"""
import fcntl
import os
from pathlib import Path
import time
import traceback

import report_data
import serial


class FixtureEvidence(report_data.Evidence):
    def stage(self,name,spec):
        original=serial.read(serial.RAW/'simple1-p1024-baseline/manifest.json')
        native=serial.read(serial.RAW/'full-population-audit-smoke0/manifest.json')
        if name=='fixture-full-native-baseline':return dict(jobs={})
        if name=='fixture-full-native-candidate':return native
        assert name in ('fixture-baseline','fixture-candidate')
        assert serial.sha(serial.RAW/'simple1-p1024-baseline/bench')==original['binary_sha256']
        original=dict(original,jobs={key+'.r0':job for key,job in original['jobs'].items()})
        return original


out=serial.RAW/'report-reader-smoke1';out.mkdir()
state=dict(pid=os.getpid(),status='waiting_for_lock',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    scope='parser fixture only, reused OLD TRAINING searches/audits, no fresh comparison, CPU executions or heldout')
serial.save(out/'manifest.json',state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        state.update(status='running',source_sha256={str(p):serial.sha(p) for p in [Path(__file__),serial.BASE/'report_data.py']})
        serial.save(out/'manifest.json',state)
        freeze=dict(tasks=['sum_of_elements','is_sorted','filter_odd','house_robber'],populations=[1024],
                    generations=8,repetitions=1,variants=dict(baseline={},candidate={}))
        evidence=FixtureEvidence();result=report_data.read_speed('fixture',freeze,evidence)
        assert len(result['runs'])==8 and len(result['native_audits'])==8
        assert result['summary']['native_audit_qualified']==4
        assert not result['summary']['all_primary_targets_reached']
        assert result['costs']['native_audit_process_s']>0
        assert result['costs']['native_logical_program_observations']==4096
        assert result['costs']['native_cpu_program_executions']>0
        state.update(status='ok',old_native_artifacts_parsed=4,old_logical_members_verified=4096,
            new_native_cpu_executions=0,artifact_sha256=evidence.hashes,finished_epoch=time.time())
except BaseException as error:
    state.update(status='failed',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(out/'manifest.json',state)
