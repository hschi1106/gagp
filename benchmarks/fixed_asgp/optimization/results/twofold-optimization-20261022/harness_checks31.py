"""Check current contracts plus development views and explicit partial-report guards."""
import fcntl
import os
from pathlib import Path
import subprocess
import time
import traceback

import serial


out = serial.RAW / "harness-checks-attempt31"
out.mkdir()
state = dict(pid=os.getpid(), status="waiting", started_epoch=time.time(), deadline_epoch=serial.DEADLINE)
serial.save(out / "manifest.json", state)
try:
    with serial.GPU_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        checked = sorted(serial.BASE.glob("*.py"))
        state.update(status="running", source_sha256={str(p): serial.sha(p) for p in checked})
        serial.save(out / "manifest.json", state)
        for path in checked:
            compile(path.read_text(), str(path), "exec")
        command = ["python3", "-m", "unittest", "discover", "-s", str(serial.BASE), "-p", "test_*.py", "-v"]
        with (out / "checks.log").open("x") as log:
            child = subprocess.Popen(command, cwd=serial.ROOT, stdout=log, stderr=subprocess.STDOUT)
            state.update(child_pid=child.pid, command=command)
            serial.save(out / "manifest.json", state)
            result = child.wait()
        assert all(serial.sha(p) == h for p, h in state["source_sha256"].items()), "study sources changed during checks"
        state.update(status="ok" if result == 0 else "failed", returncode=result, finished_epoch=time.time())
except BaseException as error:
    state.update(status="failed", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
    raise
finally:
    serial.save(out / "manifest.json", state)
