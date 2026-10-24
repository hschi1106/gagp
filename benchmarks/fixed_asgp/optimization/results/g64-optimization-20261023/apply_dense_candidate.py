"""Apply the staged dense-case implementation after all source-dependent jobs exit."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import subprocess
import time
import traceback

import build_candidate_v2
import serial


PREREQUISITES = ("capacity0", "phase1", "full-audit-cost0")


def main(name, build_name):
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists() and not (serial.RAW / build_name).exists()
    protocol = serial.read(serial.BASE / "protocol.json")
    staged = serial.read(serial.RAW / "dense3-staged.json")
    state = dict(pid=os.getpid(), status="waiting_for_source_users", started_epoch=time.time(),
                 deadline_epoch=serial.DEADLINE, prerequisites=PREREQUISITES, build_name=build_name,
                 patch=staged["patch"], patch_sha256=staged["patch_sha256"],
                 harness_sha256={str(p): serial.sha(p) for p in
                                 [Path(__file__), serial.BASE / "build_candidate_v2.py", serial.BASE / "serial.py"]})
    serial.save(record, state)
    try:
        while True:
            complete = {}
            for prior_name in PREREQUISITES:
                path = serial.RAW / (prior_name + "-controller.json")
                prior = serial.read(path) if path.exists() else {}
                if prior.get("status") == "stopped":
                    raise RuntimeError(f"Prerequisite {prior_name} stopped; inspect before changing source")
                complete[prior_name] = (prior.get("status") == "requested_phase_complete" and
                                        not Path(f"/proc/{prior.get('pid', 0)}").exists())
            if all(complete.values()):
                break
            assert time.time() < protocol["development_cutoff_epoch"], "Development cutoff; no source mutation"
            time.sleep(10)
        capacity = serial.read(serial.RAW / "capacity0-controller.json")
        for row in capacity["comparisons"]:
            if row["statuses"] == ["ok", "ok"]:
                assert all(row[k] for k in ("full_final_ast_equal", "final_descriptors_equal", "champions_equal"))
        while not (serial.RAW / "harness-checks2/manifest.json").exists():
            assert time.time() < protocol["development_cutoff_epoch"]
            time.sleep(10)
        checks = serial.read(serial.RAW / "harness-checks2/manifest.json")
        while checks["status"] in ("waiting", "running"):
            assert time.time() < protocol["development_cutoff_epoch"]
            time.sleep(10)
            checks = serial.read(serial.RAW / "harness-checks2/manifest.json")
        assert checks["status"] == "ok"
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert time.time() < protocol["development_cutoff_epoch"]
            for path, digest in state["harness_sha256"].items():
                assert serial.sha(path) == digest
            build = serial.read(serial.RAW / staged["source_build"] / "manifest.json")
            assert build["status"] == "ok" and not Path(f"/proc/{build['pid']}").exists()
            assert all(serial.sha(serial.ROOT / p) == digest for p, digest in build["source_sha256"].items()), "Source changed after staging"
            assert serial.sha(staged["patch"]) == staged["patch_sha256"]
            for prior_name in ("view3", "combined4", "training-confirm0", *PREREQUISITES):
                prior = serial.read(serial.RAW / (prior_name + "-controller.json"))
                assert not Path(f"/proc/{prior['pid']}").exists(), "A source-dependent controller still lives"
            subprocess.run(["git", "apply", "--check", staged["patch"]], cwd=serial.ROOT, check=True)
            state.update(status="applying_staged_source", source_build=staged["source_build"])
            serial.save(record, state)
            subprocess.run(["git", "apply", staged["patch"]], cwd=serial.ROOT, check=True)
            state.update(status="building_and_native_checks", applied_epoch=time.time(),
                         changed_source_sha256={p: serial.sha(serial.ROOT / p) for p in staged["changed_paths"]})
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
    args = parser.parse_args()
    main(args.name, args.build_name)
