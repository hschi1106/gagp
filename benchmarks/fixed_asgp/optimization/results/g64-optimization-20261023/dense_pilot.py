"""Fresh G64 controls for dense arena scheduling, then one global full30 recipe."""
from argparse import ArgumentParser
import fcntl
import math
import os
from pathlib import Path
import time
import traceback

import development_v2
import serial
import settings_followup_v2


def select_recipe(combined, phase):
    assert combined["status"] == phase["status"] == "requested_phase_complete"
    assert combined["task_count"] == 30
    recipes = [(combined["geometric_mean"], "combined4", combined["candidate_extra"])]
    if phase.get("full30_geometric_means"):
        recipes.append((phase["full30_geometric_means"]["candidate"], "phase1", phase["selected_extra"]))
    assert all(math.isfinite(r[0]) and r[0] > 0 for r in recipes)
    return max(recipes, key=lambda row: row[0])


def matrix(name, tasks, protocol, build, extra, harness):
    variants = {"baseline": {}, "ordinary": dict(extra, GAGP_GPU_DENSE_CASE_LANES="0"),
                "candidate": dict(extra, GAGP_GPU_DENSE_CASE_LANES="1")}
    with serial.GPU_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        stages = {v: serial.Stage(name + "-" + v,
                  protocol["baseline_binary"] if v == "baseline" else build["binary"],
                  settings=protocol["baseline_settings"]) for v in variants}
        for variant, stage in stages.items():
            stage.m.update(harness_sha256=harness, source_build="baseline" if variant == "baseline" else build["name"])
            stage.persist()
    rows = []
    activations = 0
    for index, task in enumerate(tasks):
        assert time.time() < protocol["development_cutoff_epoch"], "Keep incomplete development grid"
        order = list(variants)
        order = order[index % 3:] + order[:index % 3]
        for variant in order:
            development_v2.record_search(stages[variant], task, variant,
                                          protocol["development_inputs"][task], variants[variant])
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            jobs = {v: stage.m["jobs"][f"{task}.s0.g64"] for v, stage in stages.items()}
            compared = settings_followup_v2.compare(jobs)
            assert all(r[k] for r in compared for k in ("full_final_ast_equal", "final_descriptors_equal", "champions_equal"))
            rows.extend(compared)
            doc = serial.read(jobs["candidate"]["output"])
            activations += sum("-dense-case-lanes" in r["execution_profile"] and
                               "-dense-case-lanes-declined" not in r["execution_profile"] for r in doc["generations"])
            serial.save(serial.RAW / (name + "-all-comparisons.json"), rows)
            serial.save(serial.RAW / (name + "-comparisons.json"), [r for r in rows if r["variant"] == "candidate"])
    means = {v: math.exp(sum(math.log(r["speedup"]) for r in rows if r["variant"] == v) / len(tasks))
             for v in ("ordinary", "candidate")}
    for stage in stages.values():
        stage.m.update(status="requested_phase_complete", finished_epoch=time.time())
        stage.persist()
    return means, activations


def main(name, build_name, apply_name):
    record = serial.RAW / (name + "-controller.json")
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    assert not record.exists()
    protocol = serial.read(serial.BASE / "protocol.json")
    state = dict(pid=os.getpid(), status="waiting_for_native_checks", started_epoch=time.time(),
                 deadline_epoch=serial.DEADLINE, build_name=build_name, apply_name=apply_name,
                 harness_sha256={str(p): serial.sha(p) for p in
                                 [Path(__file__), serial.BASE / "serial.py", serial.BASE / "protocol.json",
                                  serial.BASE / "development_v2.py", serial.BASE / "settings_followup_v2.py"]})
    serial.save(record, state)
    try:
        while True:
            path = serial.RAW / build_name / "manifest.json"
            build = serial.read(path) if path.exists() else {}
            apply_path = serial.RAW / (apply_name + "-controller.json")
            applying = serial.read(apply_path) if apply_path.exists() else {}
            if build.get("status") == "failed" or applying.get("status") == "stopped":
                raise RuntimeError("Native prerequisite stopped; no candidate search")
            if build.get("status") == "ok" and not Path(f"/proc/{build['pid']}").exists():
                break
            assert time.time() < protocol["development_cutoff_epoch"], "Native prerequisite missed development cutoff"
            time.sleep(10)
        for path, digest in state["harness_sha256"].items():
            assert serial.sha(path) == digest
        assert serial.sha(build["binary"]) == build["binary_sha256"]
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert all(serial.sha(serial.ROOT / p) == digest for p, digest in build["source_sha256"].items())
        combined = serial.read(serial.RAW / "combined4-controller.json")
        phase = serial.read(serial.RAW / "phase1-controller.json")
        # One complete global TRAINING recipe, never a per-task switch or a
        # product of separately measured component gains.
        _, prior_name, extra = select_recipe(combined, phase)
        build["name"] = build_name
        state.update(status="coarse_g64", binary=build["binary"], binary_sha256=build["binary_sha256"],
                     selected_existing_recipe=prior_name, existing_extra=extra,
                     candidate_extra=dict(extra, GAGP_GPU_DENSE_CASE_LANES="1"))
        serial.save(record, state)
        means, active = matrix(name + "-coarse", development_v2.SCREEN_TASKS, protocol, build, extra, state["harness_sha256"])
        admitted = active > 0 and means["candidate"] >= 1.02 and means["candidate"] / means["ordinary"] >= 1.02
        state.update(coarse_geometric_means=means, coarse_active_generations=active, coarse_performance_admitted=admitted)
        serial.save(record, state)
        if admitted:
            state["status"] = "full30_g64"
            serial.save(record, state)
            full, active = matrix(name, protocol["tasks"], protocol, build, extra, state["harness_sha256"])
            state.update(task_count=30, geometric_mean=full["candidate"], geometric_means=full,
                         candidate_over_ordinary=full["candidate"] / full["ordinary"], active_generations=active,
                         performance_admitted=full["candidate"] >= 1.02 and full["candidate"] / full["ordinary"] >= 1.02,
                         target_observed_in_development=full["candidate"] >= 1.3)
        else:
            state.update(task_count=0, performance_admitted=False,
                         rejection="Dense scheduling did not beat both fresh coarse controls by >=2%; no full30 or target claim")
        state.update(status="requested_phase_complete", finished_epoch=time.time(),
                     caveat="Old TRAINING development only; fresh full30/G64/R3, ASGP1T and independent quality still required")
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--build-name", required=True)
    parser.add_argument("--apply-name", required=True)
    args = parser.parse_args()
    main(args.name, args.build_name, args.apply_name)
