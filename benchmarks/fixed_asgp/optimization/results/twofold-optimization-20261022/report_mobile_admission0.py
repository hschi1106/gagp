"""Prove the versioned formal report changes only presentation/provenance paths."""
import ast
import fcntl
import os
from pathlib import Path
import time
import traceback

import serial


out = serial.RAW / 'report-mobile-admission0'
out.mkdir()
state = dict(pid=os.getpid(), status='waiting', started_epoch=time.time(),
             deadline_epoch=serial.DEADLINE, formal_browser_validation='pending real formal data')
serial.save(out / 'manifest.json', state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        checks = serial.read(serial.RAW / 'harness-checks-attempt24/manifest.json')
        assert checks['status'] == 'ok'
        for path, digest in checks['source_sha256'].items():
            assert serial.sha(path) == digest, 'original checked source changed'
        record = serial.RAW / 'report-mobile-ui-change0.json'
        change = serial.read(record)
        paths = [Path(__file__), record] + [Path(change[key]) for key in
                 ('original_views', 'mobile_views', 'original_report', 'mobile_report')]
        hashes = {str(path): serial.sha(path) for path in paths}
        before = Path(change['original_views']).read_text()
        after = Path(change['mobile_views']).read_text()
        normalized = after.replace(change['mobile_css'], change['original_css'])
        normalized = normalized.replace('<table class="speed-table">', '<table>')
        normalized = normalized.replace('<table class="quality-table">', '<table>')
        assert normalized == before, 'view change is not restricted to declared CSS/classes'
        original_functions = {n.name: ast.get_source_segment(before, n)
                              for n in ast.parse(before).body if isinstance(n, ast.FunctionDef)}
        mobile_functions = {n.name: ast.get_source_segment(after, n)
                            for n in ast.parse(after).body if isinstance(n, ast.FunctionDef)}
        assert set(original_functions) == set(mobile_functions)
        assert all(original_functions[name] == mobile_functions[name]
                   for name in original_functions if name != 'dashboard')
        original_report = Path(change['original_report']).read_text()
        mobile_report = Path(change['mobile_report']).read_text()
        normalized_report = mobile_report.replace('import report_views_mobile as report_views',
                                                   'import report_views')
        normalized_report = normalized_report.replace("'report_views_mobile'", "'report_views'")
        assert normalized_report == original_report, 'analysis entry point changed'
        compile(after, change['mobile_views'], 'exec')
        compile(mobile_report, change['mobile_report'], 'exec')
        assert all(serial.sha(path) == digest for path, digest in hashes.items())
        state.update(status='ok', source_sha256=hashes, analysis_code_identical=True,
                     css_only_view_change=True, provenance_hash_path_updated=True,
                     finished_epoch=time.time())
except BaseException as error:
    state.update(status='failed', error=repr(error), traceback=traceback.format_exc(),
                 finished_epoch=time.time())
    raise
finally:
    serial.save(out / 'manifest.json', state)
