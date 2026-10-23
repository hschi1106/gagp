"""Render an explicit INTERIM view of already-checked development data."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import development_views2 as development_views
import report
import serial


out = serial.BASE / 'development-preview1'
out.mkdir()
state = dict(pid=os.getpid(), status='waiting', started_epoch=time.time(),
    deadline_epoch=serial.DEADLINE, kind='interim_development_view_smoke',
    native_searches_started=0, goal_proven=False, formal_target_proven=False,
    independent_quality_proven=False)
serial.save(out / 'completion.json', state)
try:
    for key in ('OMP_NUM_THREADS', 'OPENBLAS_NUM_THREADS', 'MKL_NUM_THREADS'):
        os.environ[key] = '1'
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        assert time.time() < serial.DEADLINE - 10800
        checks = serial.read(serial.RAW / 'harness-checks-attempt24/manifest.json')
        assert checks['status'] == 'ok' and checks['returncode'] == 0
        for path, digest in checks['source_sha256'].items():
            assert serial.sha(path) == digest
        source = serial.RAW / 'development-evidence-smoke0/manifest.json'
        previous = serial.read(source)
        assert previous['status'] == 'ok' and serial.sha(previous['result']) == previous['result_sha256']
        paths = [Path(__file__), serial.BASE / 'development_views2.py', serial.BASE / 'report.py', source]
        state.update(status='rendering', source_sha256={str(p): serial.sha(p) for p in paths},
                     input_sha256=previous['result_sha256'])
        serial.save(out / 'completion.json', state)
        result = serial.read(previous['result'])
        result['report_kind'] = state['kind']
        serial.save(out / 'summary.json', result)
        report.csv_file(out / 'development_cells.csv', result['cells'])
        report.csv_file(out / 'development_recipes.csv', result['recipes'])
        (out / 'REPORT.zh-TW.md').write_text(development_views.markdown(result))
        (out / 'dashboard.html').write_text(development_views.dashboard(
            result, serial.read(serial.BASE / 'protocol.json')['tasks']))
        development_views.plots(result, serial.read(serial.BASE / 'protocol.json')['tasks'], out)
        assert all(serial.sha(p) == digest for p, digest in state['source_sha256'].items())
        state.update(status='ok', finished_epoch=time.time(),
                     within_original_deadline=time.time() <= serial.DEADLINE,
                     artifact_sha256={p.name: serial.sha(p) for p in out.iterdir()
                                      if p.name != 'completion.json'})
except BaseException as error:
    state.update(status='failed', error=repr(error), traceback=traceback.format_exc(),
                 finished_epoch=time.time())
    raise
finally:
    serial.save(out / 'completion.json', state)
