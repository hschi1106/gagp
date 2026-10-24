"""Apply two staged execution-only changes after immutable source users exit."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import subprocess
import time
import traceback

import build_candidate_v2
import serial


PREREQUISITES = ("dense3", "untiled-dense0", "judge-cost-large0")


def main(name, build_name, checks_name):
    record = serial.RAW / (name + "-controller.json")
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    assert not record.exists() and not (serial.RAW / build_name).exists()
    protocol = serial.read(serial.BASE / "protocol.json")
    patches = [serial.read(serial.RAW / (n + "-staged.json")) for n in ("dense-tiles4", "case-view4")]
    state = dict(pid=os.getpid(), status="waiting_for_source_users", started_epoch=time.time(),
                 deadline_epoch=serial.DEADLINE, build_name=build_name, checks_name=checks_name,
                 prerequisites=PREREQUISITES, patches=patches,
                 harness_sha256={str(p): serial.sha(p) for p in
                                 [Path(__file__), serial.BASE / "build_candidate_v2.py", serial.BASE / "serial.py"]})
    serial.save(record, state)
    try:
        while True:
            ready = True
            for prior_name in PREREQUISITES:
                path = serial.RAW / (prior_name + "-controller.json")
                prior = serial.read(path) if path.exists() else {}
                if prior.get("status") == "stopped":
                    raise RuntimeError(f"Inspect stopped prerequisite {prior_name}; no source mutation")
                ready &= prior.get("status") == "requested_phase_complete" and not Path(f"/proc/{prior.get('pid', 0)}").exists()
            check_path = serial.RAW / checks_name / "manifest.json"
            checks = serial.read(check_path) if check_path.exists() else {}
            if checks.get("status") == "failed":
                raise RuntimeError("Python guards failed; no source mutation")
            ready &= checks.get("status") == "ok" and not Path(f"/proc/{checks.get('pid', 0)}").exists()
            if ready:
                break
            assert time.time() < protocol["development_cutoff_epoch"]
            time.sleep(10)
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert time.time() < protocol["development_cutoff_epoch"]
            for path, digest in state["harness_sha256"].items():
                assert serial.sha(path) == digest
            previous = serial.read(serial.RAW / "build-attempt3/manifest.json")
            assert previous["status"] == "ok" and not Path(f"/proc/{previous['pid']}").exists()
            assert all(serial.sha(serial.ROOT / p) == digest for p, digest in previous["source_sha256"].items())
            for patch in patches:
                assert serial.sha(patch["patch"]) == patch["patch_sha256"]
                subprocess.run(["git", "apply", "--check", patch["patch"]], cwd=serial.ROOT, check=True)
            state.update(status="applying_staged_source", source_build="build-attempt3")
            serial.save(record, state)
            for patch in patches:
                subprocess.run(["git", "apply", patch["patch"]], cwd=serial.ROOT, check=True)
            state.update(status="building_and_native_checks", applied_epoch=time.time())
            serial.save(record, state)
        build_candidate_v2.run(build_name)
        state.update(status="requested_phase_complete", finished_epoch=time.time())
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--build-name", required=True)
    parser.add_argument("--checks-name", required=True)
    args = parser.parse_args()
    main(args.name, args.build_name, args.checks_name)
