"""Fresh controls for a host-proved VM layout specialization, OLD TRAINING only."""
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


def parent_recipe(priors):
    choices = []
    for name, prior in priors.items():
        assert prior["status"] == "requested_phase_complete"
        if prior.get("task_count") != 30:
            continue
        ordinary = prior.get("parent_extra", prior["arms"]["ordinary"])
        for variant, extra in (("ordinary", ordinary), ("candidate", prior["candidate_extra"])):
            choices.append((prior["geometric_means"][variant], name, variant, extra))
    assert choices, "Need an actually measured full30 parent recipe"
    return max(choices, key=lambda row: row[0])


def main(name, apply_name):
    record = serial.RAW / (name + "-controller.json")
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    assert not record.exists()
    p = serial.read(serial.BASE / "protocol.json")
    state = dict(pid=os.getpid(), status="waiting_for_native", started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 source_build="build-attempt5", harness_sha256={str(path): serial.sha(path) for path in
                 [Path(__file__), serial.BASE / "tiles_pilot4.py", serial.BASE / "serial.py", serial.BASE / "development_v2.py",
                  serial.BASE / "settings_followup_v2.py", serial.BASE / "protocol.json"]})
    serial.save(record, state)
    try:
        while True:
            a = serial.RAW / (apply_name + "-controller.json")
            applying = serial.read(a) if a.exists() else {}
            if applying.get("status") == "budget_not_run":
                state.update(status="budget_not_run", finished_epoch=time.time(), no_samples=True)
                return
            path = serial.RAW / "build-attempt5/manifest.json"
            build = serial.read(path) if path.exists() else {}
            if applying.get("status") == "stopped" or build.get("status") == "failed":
                raise RuntimeError("Source/native prerequisite failed; no candidate searches")
            if build.get("status") == "ok" and not Path(f"/proc/{build['pid']}").exists():
                break
            assert time.time() < p["development_cutoff_epoch"]
            time.sleep(10)
        for path, digest in state["harness_sha256"].items():
            assert serial.sha(path) == digest
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert all(serial.sha(serial.ROOT / path) == digest for path, digest in build["source_sha256"].items())
        value, prior, variant, parent = parent_recipe({n: serial.read(serial.RAW / (n + "-controller.json")) for n in ("tiles4", "geometry5")})
        ordinary = dict(parent, GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT="0")
        control = dict(ordinary, GAGP_GPU_PRIVATE_LAUNCH_BOUND="32")
        candidate = dict(control, GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT="1")
        build["name"] = "build-attempt5"
        arms = dict(baseline={}, ordinary=ordinary, control32=control, candidate=candidate)
        state.update(status="coarse_g64", parent=(value, prior, variant), parent_extra=parent,
                     binary=build["binary"], binary_sha256=build["binary_sha256"], candidate_extra=candidate, arms=arms)
        serial.save(record, state)
        means, rows = tiles_pilot4.matrix(name + "-coarse", development_v2.SCREEN_TASKS, arms, p, build, state["harness_sha256"])
        active = 0
        for task in development_v2.SCREEN_TASKS:
            doc = serial.read(serial.RAW / (name + "-coarse-candidate") / (task + ".s0.g64.json"))
            active += sum("-private-layout-specialized" in g["execution_profile"] and
                          "-private-layout-specialized-declined" not in g["execution_profile"] for g in doc["generations"])
        admitted = active > 0 and means["candidate"] >= 1.02 and means["candidate"] / max(means["ordinary"], means["control32"]) >= 1.02
        state.update(coarse_geometric_means=means, coarse_active_generations=active, coarse_performance_admitted=admitted)
        serial.save(record, state)
        if admitted:
            state["status"] = "full30_g64"
            serial.save(record, state)
            full, _ = tiles_pilot4.matrix(name, p["tasks"], arms, p, build, state["harness_sha256"])
            state.update(task_count=30, geometric_mean=full["candidate"], geometric_means=full,
                         performance_admitted=full["candidate"] >= 1.02 and full["candidate"] / max(full["ordinary"], full["control32"]) >= 1.02,
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
    parser.add_argument("--apply-name", required=True)
    args = parser.parse_args()
    main(args.name, args.apply_name)
