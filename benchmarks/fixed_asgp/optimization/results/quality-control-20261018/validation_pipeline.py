"""Wait for the authorized diagnostics, freeze from training, run fresh validation."""
import os
from pathlib import Path
import time
import traceback

import study
import freeze_validation
import run_validation

record=study.RAW/'validation-pipeline-controller.json'
if record.exists():raise RuntimeError('pipeline attempt exists; inspect it before any explicit recovery')
state=dict(pid=os.getpid(),status='waiting_for_diagnostics',started_epoch=time.time(),deadline_epoch=study.DEADLINE)
study.save(record,state)
required=['initial','tuple-pilot','site-admission','development','checkpoint-audit','site-development',
          'site-training-judges','development-training-judges','guard-development-v2','validation-harness-smoke','checkpoint-metrics']
try:
    while True:
        status={}
        for name in required:
            path=study.RAW/name/'manifest.json'
            status[name]=study.read(path)['status'] if path.exists() else 'not_started'
        state['prerequisites']=status;study.save(record,state)
        if all(value=='complete' for value in status.values()):
            try:run_validation.preflight()
            except RuntimeError:time.sleep(5);continue
            break
        if time.time()>study.DEADLINE-8*3600:raise RuntimeError('diagnostics exceeded validation start window; manual partial-coverage review required')
        time.sleep(15)
    path=freeze_validation.freeze()
    state.update(status='frozen',freeze=str(path),freeze_sha256=study.sha(path));study.save(record,state)
    study.save(study.RAW/'formal-selection.json',dict(selection=study.read(path),recorded_epoch=time.time()))
    # This is the first preparation/use of the fresh held-out pool.
    state['status']='validation';study.save(record,state)
    run_validation.run(path)
    state.update(status='requested_phase_complete',finished_epoch=time.time());study.save(record,state)
except BaseException as error:
    state.update(status='failed',error=str(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    study.save(record,state)
    raise
