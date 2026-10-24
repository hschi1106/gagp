"""Test dense scheduling with coalesced private cells/exact short-list scoring."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import subprocess
import time
import traceback

import development_v2
import runtime_forecast_v2
import serial
import tiles_pilot4


def recipes(parent):
    ordinary = dict(parent)
    soa = dict(ordinary, GAGP_GPU_PRIVATE_CELL_SOA="1")
    short = dict(ordinary, GAGP_GPU_SHORT_EDIT_DISTANCE="1")
    both = dict(soa, GAGP_GPU_SHORT_EDIT_DISTANCE="1")
    # Case tiles were tested with contiguous cells, where full active warps
    # can increase memory pressure. Test their interaction with coalescing.
    return dict(baseline={}, ordinary=ordinary, soa=soa, short=short, soa_short=both,
                soa_short_dense=dict(both, GAGP_GPU_DENSE_CASE_TILES="1"))


def budget(now, allowance):
    costs, assumptions = runtime_forecast_v2.measured_costs("combined4", "training-confirm0")
    result = runtime_forecast_v2.choose_scale(now + allowance, costs)
    result.update(diagnostic_only=True, allowance_s=allowance, assumptions=assumptions,
                  caveat="Prior old-TRAINING costs only; final selected candidate and actual clock must be recomputed")
    return result


def main(name, prior_name, checks_name):
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists()
    p = serial.read(serial.BASE / "protocol.json")
    state = dict(pid=os.getpid(), status="waiting_for_full30_parent", started_epoch=time.time(),
                 deadline_epoch=serial.DEADLINE, prior=prior_name, checks=checks_name, new_heldout=False,
                 scope="OldTRAINING8 coarse -> one global full30 candidate -> three oldseeds/native; no paper/test labels",
                 hypothesis="Coalesced private cells may change dense-tile memory pressure; short edit scoring remains exact",
                 harness_sha256={str(f): serial.sha(f) for f in [Path(__file__), serial.BASE / "tiles_pilot4.py",
                    serial.BASE / "serial.py", serial.BASE / "development_v2.py", serial.BASE / "settings_followup_v2.py",
                    serial.BASE / "runtime_forecast_v2.py", serial.BASE / "runtime_forecast.py", serial.BASE / "protocol.json"]})
    serial.save(record, state)
    try:
        while True:
            path = serial.RAW / (prior_name + "-controller.json")
            prior = serial.read(path) if path.exists() else {}
            if prior.get("status") == "stopped":
                raise RuntimeError("Prior source/search checks failed")
            if prior.get("status") == "requested_phase_complete" and not Path(f"/proc/{prior['pid']}").exists():
                break
            assert time.time() < p["development_cutoff_epoch"]
            time.sleep(10)
        while True:
            path = serial.RAW / checks_name / "manifest.json"
            checks = serial.read(path) if path.exists() else {}
            if checks.get("status") == "failed":
                raise RuntimeError("New scientific harness guards failed")
            if checks.get("status") == "ok" and not Path(f"/proc/{checks['pid']}").exists():
                break
            assert time.time() < p["development_cutoff_epoch"]
            time.sleep(5)
        assert checks["source_sha256"][str(Path(__file__))] == serial.sha(__file__)
        assert prior["performance_admitted"] and prior["task_count"] == 30
        assert not list(serial.RAW.glob("*-quality-freeze.json"))
        for f, digest in state["harness_sha256"].items():
            assert serial.sha(f) == digest
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            build = serial.read(serial.RAW / prior["source_build"] / "manifest.json")
            assert build["status"] == "ok" and not Path(f"/proc/{build['pid']}").exists()
            assert all(serial.sha(serial.ROOT / f) == digest for f, digest in build["source_sha256"].items())
            assert serial.sha(build["binary"]) == build["binary_sha256"] == prior["binary_sha256"]
            forecast = budget(time.time(), 420)
            serial.save(serial.RAW / (name + "-coarse-forecast.json"), forecast)
            if forecast["selected_seed_count"] is None:
                state.update(status="budget_not_run", no_samples=True, finished_epoch=time.time())
                return
        arms = recipes(prior["candidate_extra"])
        build["name"] = prior["source_build"]
        state.update(status="coarse_g64", arms=arms, source_build=prior["source_build"],
                     binary=build["binary"], binary_sha256=build["binary_sha256"])
        serial.save(record, state)
        means, rows = tiles_pilot4.matrix(name + "-coarse", development_v2.SCREEN_TASKS, arms, p, build, state["harness_sha256"])
        winner = max(("soa", "short", "soa_short", "soa_short_dense"), key=means.get)
        activation = {}
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            for variant in ("soa", "short", "soa_short", "soa_short_dense"):
                counts = dict(soa=0, short=0)
                for task in development_v2.SCREEN_TASKS:
                    doc = serial.read(serial.RAW / (name + "-coarse-" + variant) / (task + ".s0.g64.json"))
                    for generation in doc["generations"]:
                        profile = generation["execution_profile"]
                        counts["soa"] += "-private-cell-soa" in profile and "-private-cell-soa-declined" not in profile
                        counts["short"] += "-short-edit-exact" in profile and "-short-edit-declined" not in profile
                activation[variant] = counts
            admitted = means[winner] >= 1.02 and means[winner] / means["ordinary"] >= 1.02
            if "soa" in winner and activation[winner]["soa"] == 0:
                admitted = False
            if "short" in winner and activation[winner]["short"] == 0:
                admitted = False
            state.update(coarse_geometric_means=means, coarse_winner=winner, coarse_admitted=admitted,
                         activation=activation, task_count=0, performance_admitted=False)
            serial.save(record, state)
            forecast = budget(time.time(), 1200)
            serial.save(serial.RAW / (name + "-full-forecast.json"), forecast)
        if admitted and forecast["selected_seed_count"] is not None:
            state.update(status="full30_training_controls", candidate_extra=arms[winner])
            serial.save(record, state)
            full, _ = tiles_pilot4.matrix(name, p["tasks"],
                {"baseline": {}, "ordinary": arms["ordinary"], "candidate": arms[winner]}, p, build, state["harness_sha256"])
            state.update(task_count=30, geometric_mean=full["candidate"], geometric_means=full,
                         performance_admitted=full["candidate"] >= 1.02 and full["candidate"] / full["ordinary"] >= 1.02,
                         target_observed_in_development=full["candidate"] >= 1.3)
        elif admitted:
            state["full_not_run_reason"] = "Projected full30 plus confirmation no longer fits the original minimum formal grid"
        state.update(status="requested_phase_complete", finished_epoch=time.time())
        if state["performance_admitted"]:
            confirmation = name + "-confirm"
            assert not (serial.RAW / (confirmation + "-controller.json")).exists()
            with (serial.RAW / (confirmation + ".log")).open("x") as log:
                child = subprocess.Popen(["python3", "-u", str(serial.BASE / "training_confirm.py"), "--name", confirmation,
                    "--prior-name", name], cwd=serial.ROOT, env=dict(os.environ, PYTHONPATH=str(serial.ROOT / "tools")),
                    stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            state.update(next_confirmation=confirmation, next_confirmation_pid=child.pid)
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--prior", required=True)
    parser.add_argument("--checks", required=True)
    args = parser.parse_args()
    main(args.name, args.prior, args.checks)
