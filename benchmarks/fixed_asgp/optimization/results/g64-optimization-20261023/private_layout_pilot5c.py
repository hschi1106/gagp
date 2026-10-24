"""Full30 TRAINING ablations, then a fresh selected-recipe confirmation pair."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import subprocess
import time
import traceback

from private_layout_pilot5 import parent_recipe
import serial
import tiles_pilot4


def recipes(parent):
    ordinary = dict(parent, GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT="0")
    control = dict(ordinary, GAGP_GPU_PRIVATE_LAUNCH_BOUND="32")
    hybrid = dict(control, GAGP_GPU_VIEW_PHYSICAL_BLOCK="0", GAGP_GPU_CASE_VIEW_BLOCK="hybrid",
                  GAGP_GPU_DENSE_CASE_LANES="0", GAGP_GPU_DENSE_CASE_TILES="tuple")
    return dict(baseline={}, ordinary=ordinary, control32=control, hybrid=hybrid,
                specialized_parent=dict(control, GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT="1"),
                combined=dict(hybrid, GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT="1"))


def main(name, apply_name):
    record = serial.RAW / (name + "-controller.json")
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    assert not record.exists()
    p = serial.read(serial.BASE / "protocol.json")
    state = dict(pid=os.getpid(), status="waiting_for_native", started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 source_build="build-attempt5", new_heldout=False,
                 scope="All30 oldTRAINING before selection, no coarse8 extrapolation; policy-only/compiler-only/combined controls",
                 harness_sha256={str(path): serial.sha(path) for path in
                 [Path(__file__), serial.BASE / "private_layout_pilot5.py", serial.BASE / "tiles_pilot4.py",
                  serial.BASE / "serial.py", serial.BASE / "development_v2.py", serial.BASE / "settings_followup_v2.py", serial.BASE / "protocol.json"]})
    serial.save(record, state)
    try:
        while True:
            path = serial.RAW / (apply_name + "-controller.json")
            applying = serial.read(path) if path.exists() else {}
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
        assert not list(serial.RAW.glob("*-quality-freeze.json"))
        for path, digest in state["harness_sha256"].items():
            assert serial.sha(path) == digest
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert all(serial.sha(serial.ROOT / path) == digest for path, digest in build["source_sha256"].items())
        value, prior, variant, parent = parent_recipe({n: serial.read(serial.RAW / (n + "-controller.json")) for n in ("tiles4", "geometry5")})
        arms = recipes(parent)
        build["name"] = "build-attempt5"
        state.update(status="full30_ablation_sweep", parent=(value, prior, variant), parent_extra=parent, arms=arms,
                     binary=build["binary"], binary_sha256=build["binary_sha256"])
        serial.save(record, state)
        means, rows = tiles_pilot4.matrix(name + "-sweep", p["tasks"], arms, p, build, state["harness_sha256"])
        eligible = ("control32", "hybrid", "specialized_parent", "combined")
        winner = max(eligible, key=means.get)
        admitted = means[winner] >= 1.02 and means[winner] / means["ordinary"] >= 1.02
        activation = {}
        for variant in ("specialized_parent", "combined"):
            activation[variant] = 0
            for task in p["tasks"]:
                doc = serial.read(serial.RAW / (name + "-sweep-" + variant) / (task + ".s0.g64.json"))
                activation[variant] += sum("-private-layout-specialized" in g["execution_profile"] and
                    "-private-layout-specialized-declined" not in g["execution_profile"] for g in doc["generations"])
        if winner in activation and activation[winner] == 0:
            admitted = False
        state.update(sweep_geometric_means=means, sweep_winner=winner, sweep_performance_admitted=admitted,
                     specialized_active_generations=activation)
        serial.save(record, state)
        if admitted:
            state.update(status="fresh_selected_full30_controls", candidate_extra=arms[winner])
            serial.save(record, state)
            fresh, _ = tiles_pilot4.matrix(name, p["tasks"], {"baseline": {}, "candidate": arms[winner]},
                                           p, build, state["harness_sha256"])
            state.update(task_count=30, geometric_mean=fresh["candidate"], geometric_means=fresh,
                         performance_admitted=fresh["candidate"] >= 1.02,
                         target_observed_in_development=fresh["candidate"] >= 1.3)
        else:
            state.update(task_count=0, performance_admitted=False)
        state.update(status="requested_phase_complete", finished_epoch=time.time())
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
