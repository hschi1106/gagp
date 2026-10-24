"""Optional source attempt bounded to preserve this round's full validation."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import subprocess
import time
import traceback

import build_candidate_v3
import serial


def main(name, checks_name):
    record = serial.RAW / (name + "-controller.json")
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    assert not record.exists() and not (serial.RAW / "build-attempt5").exists()
    staged = serial.read(serial.RAW / "private-layout5b-staged.json")
    state = dict(pid=os.getpid(), status="waiting_for_source_users", started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 optional_build_cutoff=serial.START + 16200, patch=staged, checks=checks_name,
                 harness_sha256={str(p): serial.sha(p) for p in [Path(__file__), serial.BASE / "build_candidate_v3.py", serial.BASE / "serial.py"]})
    serial.save(record, state)
    try:
        while True:
            if time.time() >= state["optional_build_cutoff"]:
                state.update(status="budget_not_run", finished_epoch=time.time(), source_not_changed=True,
                             reason="Preserve full formal G64 grid; do not start an optional build after START+4.5h")
                return
            done = True
            for prior_name in ("tiles4", "geometry5", "evidence-readback0"):
                path = serial.RAW / (prior_name + "-controller.json")
                prior = serial.read(path) if path.exists() else {}
                if prior.get("status") == "stopped":
                    raise RuntimeError("Inspect stopped prerequisite: " + prior_name)
                done &= prior.get("status") == "requested_phase_complete" and not Path(f"/proc/{prior.get('pid',0)}").exists()
                if prior.get("next_confirmation"):
                    path = serial.RAW / (prior["next_confirmation"] + "-controller.json")
                    conf = serial.read(path) if path.exists() else {}
                    if conf.get("status") == "stopped":
                        raise RuntimeError("Prior native/trajectory confirmation stopped")
                    done &= conf.get("status") == "requested_phase_complete" and not Path(f"/proc/{conf.get('pid',0)}").exists()
            path = serial.RAW / checks_name / "manifest.json"
            checks = serial.read(path) if path.exists() else {}
            if checks.get("status") == "failed":
                raise RuntimeError("Harness guards failed")
            done &= checks.get("status") == "ok" and not Path(f"/proc/{checks.get('pid',0)}").exists()
            if done:
                break
            time.sleep(10)
        assert not list(serial.RAW.glob("*-quality-freeze.json")), "Never mutate/tune after fresh freeze"
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert time.time() < state["optional_build_cutoff"]
            for path, digest in state["harness_sha256"].items():
                assert serial.sha(path) == digest
            build = serial.read(serial.RAW / staged["source_build"] / "manifest.json")
            assert build["status"] == "ok" and not Path(f"/proc/{build['pid']}").exists()
            assert all(serial.sha(serial.ROOT / p) == digest for p, digest in build["source_sha256"].items())
            assert serial.sha(staged["patch"]) == staged["patch_sha256"]
            subprocess.run(["git", "apply", "--check", staged["patch"]], cwd=serial.ROOT, check=True)
            state["status"] = "applying_staged_source"
            serial.save(record, state)
            subprocess.run(["git", "apply", staged["patch"]], cwd=serial.ROOT, check=True)
            state.update(status="building_and_native_checks", applied_epoch=time.time())
            serial.save(record, state)
        build_candidate_v3.run("build-attempt5")
        state.update(status="requested_phase_complete", finished_epoch=time.time())
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--checks-name", required=True)
    args = parser.parse_args()
    main(args.name, args.checks_name)
