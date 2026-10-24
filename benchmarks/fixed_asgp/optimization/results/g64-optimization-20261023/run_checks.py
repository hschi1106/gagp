"""Run this study's Python guards under the same exclusion lock as timings."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import signal
import subprocess
import time
import traceback

import serial


def main(name):
    folder = serial.RAW / name
    folder.mkdir()
    state = dict(pid=os.getpid(), status="waiting", started_epoch=time.time(), deadline_epoch=serial.DEADLINE)
    serial.save(folder / "manifest.json", state)
    try:
        with serial.GPU_LOCK.open("a") as lock:
            while True:
                try:
                    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    break
                except BlockingIOError:
                    if time.time() >= serial.DEADLINE - 14400:
                        raise RuntimeError("Checks missed timed cutoff")
                    time.sleep(.2)
            paths = sorted(serial.BASE.glob("*.py"))
            state.update(status="running", source_sha256={str(p): serial.sha(p) for p in paths})
            command = ["python3", "-m", "unittest", "discover", "-s", str(serial.BASE), "-p", "test_*.py", "-v"]
            state["command"] = command
            with (folder / "checks.log").open("x") as log:
                child = subprocess.Popen(command, cwd=serial.ROOT, stdout=log, stderr=subprocess.STDOUT,
                                         env=dict(os.environ, PYTHONPATH=str(serial.ROOT / "tools")), start_new_session=True)
                state["child_pid"] = child.pid
                serial.save(folder / "manifest.json", state)
                try:
                    result = child.wait(timeout=min(120, serial.DEADLINE - 14400 - time.time()))
                except subprocess.TimeoutExpired:
                    os.killpg(child.pid, signal.SIGTERM)
                    child.wait(timeout=5)
                    raise RuntimeError("Python checks timed out")
            assert all(serial.sha(path) == digest for path, digest in state["source_sha256"].items()), "Study source changed during tests"
            state.update(status="ok" if result == 0 else "failed", returncode=result, finished_epoch=time.time())
            if result:
                raise RuntimeError("Study Python guards failed; keep logs")
    except BaseException as error:
        state.update(status="failed", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(folder / "manifest.json", state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    args = parser.parse_args()
    main(args.name)
