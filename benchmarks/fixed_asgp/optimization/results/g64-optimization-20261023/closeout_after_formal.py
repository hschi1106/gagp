"""Run actual final regressions/report after all formal timing children exit."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import signal
import subprocess
import time
import traceback

import serial


TERMINAL = {"validation_stages_finished", "stopped", "budget_insufficient"}


def assert_quiescent(controller, manifests, alive):
    assert controller["status"] in TERMINAL
    assert not alive(controller["pid"]), "Formal controller is still active"
    for phase in controller.get("phases", {}).values():
        assert not phase.get("pid") or not alive(phase["pid"]), "Formal phase child still active"
    for document in manifests:
        pid = document.get("controller_pid", document.get("pid"))
        assert not pid or not alive(pid), "Formal stage controller still active"
        for job in document.get("jobs", {}).values():
            assert not job.get("pid") or not alive(job["pid"]), "Formal sample child still active"
            assert job.get("status") not in ("running", "waiting"), "Unresolved sample remains explicit; inspect first"


def run_command(state, record, folder, label, command, timeout, owns_lock=True):
    assert time.time() < serial.DEADLINE - 600, "Leave final delivery time"
    step = dict(command=list(map(str, command)), status="waiting", timeout_s=timeout)
    state.setdefault("steps", {})[label] = step
    state["status"] = label
    serial.save(record, state)
    lock = serial.GPU_LOCK.open("a") if owns_lock else None
    try:
        if lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
        remaining = min(timeout, serial.DEADLINE - 600 - time.time())
        assert remaining > 0
        step.update(status="running", started_epoch=time.time())
        with (folder / (label + ".log")).open("x") as log:
            child = subprocess.Popen(step["command"], cwd=serial.ROOT, stdout=log, stderr=subprocess.STDOUT,
                                     env=dict(os.environ, PYTHONPATH=str(serial.ROOT / "tools"), GAGP_CUDA_DEVICE="0",
                                              GAGP_HOST_THREADS="1", OMP_NUM_THREADS="1", OPENBLAS_NUM_THREADS="1", MKL_NUM_THREADS="1"), start_new_session=True)
            step["pid"] = child.pid
            serial.save(record, state)
            try:
                result = child.wait(timeout=remaining)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGTERM)
                try:
                    child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(child.pid, signal.SIGKILL)
                    child.wait()
                step.update(status="timeout", finished_epoch=time.time(), returncode=child.returncode)
                raise RuntimeError(label + " timed out; preserve this attempt")
        step.update(status="ok" if result == 0 else "failed", returncode=result,
                    finished_epoch=time.time(), process_s=time.time()-step["started_epoch"])
        serial.save(record, state)
        assert result == 0, label + " failed; no implicit retry"
    finally:
        if lock:
            lock.close()
        serial.save(record, state)


def main(name, formal_name, out):
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists() and not out.exists()
    folder = serial.RAW / name
    folder.mkdir()
    state = dict(pid=os.getpid(), status="waiting_for_formal", formal=formal_name,
                 started_epoch=time.time(), deadline_epoch=serial.DEADLINE, report_out=str(out),
                 harness_sha256={str(Path(__file__)): serial.sha(__file__)})
    serial.save(record, state)
    try:
        while True:
            path = serial.RAW / (formal_name + "-controller.json")
            formal = serial.read(path) if path.exists() else {}
            if formal.get("status") in TERMINAL and not Path(f"/proc/{formal['pid']}").exists():
                break
            if not formal and time.time() >= serial.DEADLINE - 14400:
                state.update(status="not_run_no_formal", finished_epoch=time.time(),
                             reason="No frozen formal study; root must report actual development coverage")
                return
            assert time.time() < serial.DEADLINE - 600
            time.sleep(15)
        if formal["status"] == "budget_insufficient":
            state.update(status="not_run_budget_insufficient", finished_epoch=time.time())
            return
        manifests = [serial.read(f) for f in sorted(serial.RAW.glob(formal_name + "*/manifest.json"))]
        assert_quiescent(formal, manifests, lambda pid: Path(f"/proc/{pid}").exists())
        freeze = serial.read(serial.RAW / (formal_name + "-quality-freeze.json"))
        provenance = serial.read(freeze["source_provenance"])
        source = provenance["cpp_source_sha256"]
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert all(serial.sha(serial.ROOT / f) == digest for f, digest in source.items())
            state["source_sha256"] = source
            state["study_source_sha256"] = {str(f): serial.sha(f) for f in sorted(serial.BASE.glob("*.py"))}
            serial.save(record, state)
        for label, command, timeout in [
            ("full_release_build", ["cmake", "--build", "cpp/build/release", "-j", "2"], 3600),
            ("native_all", ["ctest", "--test-dir", "cpp/build/release", "--output-on-failure"], 1800),
            ("study_guards", ["python3", "-m", "unittest", "discover", "-s", str(serial.BASE), "-p", "test_*.py", "-v"], 120),
            ("tools", ["python3", "-m", "unittest", "discover", "-s", "tools/tests", "-p", "test_*.py", "-v"], 300),
            ("repository", ["python3", "-m", "unittest", "discover", "-s", "tests/repository", "-p", "test_*.py", "-v"], 300)]:
            run_command(state, record, folder, label, command, timeout)
        assert all(serial.sha(serial.ROOT / f) == digest for f, digest in source.items())
        assert all(serial.sha(f) == digest for f, digest in state["study_source_sha256"].items())
        run_command(state, record, folder, "report", ["python3", "-u", str(serial.BASE / "report.py"),
                    "--formal", formal_name, "--out", str(out)], 7200, owns_lock=False)
        state.update(status="artifacts_ready_manual_review_pending", finished_epoch=time.time(),
                     remaining="Inspect actual plots; HTML728/360 interactions/nooverflow; affected docs, diff/untracked, local commit/no push, original HTML render, disable monitor")
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--formal", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    main(args.name, args.formal, args.out.resolve())
