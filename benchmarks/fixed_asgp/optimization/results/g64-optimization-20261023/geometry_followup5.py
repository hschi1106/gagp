"""Old-TRAINING whole-recipe geometry controls after the dense-tile attempt."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import subprocess
import time
import traceback

import development_v2
import serial
import tiles_pilot4


def recipes(parent):
    result = {"baseline": {}, "ordinary": dict(parent)}
    # Reproduction block1 is the current baseline. Whole recipes below
    # measure interaction with the actual admitted dense/adaptive layout.
    for bound in (32, 512, 1024):
        for threads in (1, 2, 4, 8):
            result[f"launch{bound}_repro{threads}"] = dict(parent, GAGP_GPU_PRIVATE_LAUNCH_BOUND=str(bound),
                                                         GAGP_PHASE_REPRO_THREADS=str(threads))
    for threads in (2, 4, 8):
        result[f"repro{threads}"] = dict(parent, GAGP_PHASE_REPRO_THREADS=str(threads))
    return result


def main(name, prior_name, checks_name):
    record = serial.RAW / (name + "-controller.json")
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    assert not record.exists()
    p = serial.read(serial.BASE / "protocol.json")
    state = dict(pid=os.getpid(), status="waiting_for_prior", started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 prior=prior_name, checks=checks_name,
                 harness_sha256={str(path): serial.sha(path) for path in
                                 [Path(__file__), serial.BASE / "tiles_pilot4.py", serial.BASE / "serial.py",
                                  serial.BASE / "development_v2.py", serial.BASE / "settings_followup_v2.py", serial.BASE / "protocol.json"]})
    serial.save(record, state)
    try:
        while True:
            path = serial.RAW / (prior_name + "-controller.json")
            prior = serial.read(path) if path.exists() else {}
            if prior.get("status") == "stopped":
                raise RuntimeError("Prior attempt stopped; preserve and inspect, no automatic retry")
            done = prior.get("status") == "requested_phase_complete" and not Path(f"/proc/{prior.get('pid',0)}").exists()
            if prior.get("next_confirmation"):
                path = serial.RAW / (prior["next_confirmation"] + "-controller.json")
                confirmation = serial.read(path) if path.exists() else {}
                if confirmation.get("status") == "stopped":
                    raise RuntimeError("Prior native/trajectory confirmation stopped")
                done &= confirmation.get("status") == "requested_phase_complete" and not Path(f"/proc/{confirmation.get('pid',0)}").exists()
            path = serial.RAW / checks_name / "manifest.json"
            checks = serial.read(path) if path.exists() else {}
            if checks.get("status") == "failed":
                raise RuntimeError("Harness guards failed")
            done &= checks.get("status") == "ok" and not Path(f"/proc/{checks.get('pid',0)}").exists()
            if done:
                break
            assert time.time() < p["development_cutoff_epoch"]
            time.sleep(10)
        assert not list(serial.RAW.glob("*-quality-freeze.json")), "No tuning after a fresh cohort freeze"
        build = serial.read(serial.RAW / "build-attempt4/manifest.json")
        assert build["status"] == "ok" and not Path(f"/proc/{build['pid']}").exists()
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            for path, digest in state["harness_sha256"].items():
                assert serial.sha(path) == digest
            assert all(serial.sha(serial.ROOT / path) == digest for path, digest in build["source_sha256"].items())
        if prior.get("task_count") == 30:
            parent_variant = max(prior["geometric_means"], key=prior["geometric_means"].get)
            parent = prior["candidate_extra"] if parent_variant == "candidate" else prior["arms"]["ordinary"]
            parent_scope = "best complete30 oldTRAINING recipe, never a coarse-only gain"
        else:
            parent = prior["arms"]["ordinary"]
            parent_scope = "ordinary global recipe; previous native attempt had no admitted full30"
        arms = recipes(parent)
        build["name"] = "build-attempt4"
        state.update(status="coarse_g64", parent_extra=parent, parent_scope=parent_scope, arms=arms,
                     source_build=build["name"], binary=build["binary"], binary_sha256=build["binary_sha256"])
        serial.save(record, state)
        means, rows = tiles_pilot4.matrix(name + "-coarse", development_v2.SCREEN_TASKS, arms, p, build, state["harness_sha256"])
        winner = max(means, key=means.get)
        admitted = means[winner] >= 1.02 and means[winner] / means["ordinary"] >= 1.02
        state.update(coarse_geometric_means=means, coarse_winner=winner, coarse_performance_admitted=admitted)
        serial.save(record, state)
        if admitted:
            state.update(status="full30_g64", candidate_extra=arms[winner])
            serial.save(record, state)
            full, _ = tiles_pilot4.matrix(name, p["tasks"], {"baseline": {}, "ordinary": parent, "candidate": arms[winner]},
                                          p, build, state["harness_sha256"])
            state.update(task_count=30, geometric_mean=full["candidate"], geometric_means=full,
                         performance_admitted=full["candidate"] >= 1.02 and full["candidate"] / full["ordinary"] >= 1.02,
                         target_observed_in_development=full["candidate"] >= 1.3)
        else:
            state.update(task_count=0, performance_admitted=False)
        state.update(status="requested_phase_complete", finished_epoch=time.time(), new_heldout=False)
        if state["performance_admitted"]:
            confirm_name = name + "-confirm"
            assert not (serial.RAW / (confirm_name + "-controller.json")).exists()
            with (serial.RAW / (confirm_name + ".log")).open("x") as log:
                child = subprocess.Popen(["python3", "-u", str(serial.BASE / "training_confirm.py"), "--name", confirm_name,
                                          "--prior-name", name], cwd=serial.ROOT, env=dict(os.environ, PYTHONPATH=str(serial.ROOT / "tools")),
                                         stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            state.update(next_confirmation=confirm_name, next_confirmation_pid=child.pid)
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--prior-name", required=True)
    parser.add_argument("--checks-name", required=True)
    args = parser.parse_args()
    main(args.name, args.prior_name, args.checks_name)
