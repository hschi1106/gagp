"""Fresh old-TRAINING controls for dense initial/replay case tiles and sparse views."""
from argparse import ArgumentParser
import fcntl
import math
import os
from pathlib import Path
import subprocess
import time
import traceback

import development_v2
import serial
import settings_followup_v2


def recipes(existing, initial_tiling):
    ordinary = dict(existing, GAGP_GPU_DENSE_CASE_LANES="0", GAGP_GPU_DENSE_CASE_TILES="0", GAGP_GPU_CASE_VIEW_BLOCK="0")
    tiled = dict(ordinary, GAGP_GPU_TILED_INITIAL_PAYLOAD=initial_tiling,
                 GAGP_GPU_DENSE_CASE_LANES="1", GAGP_GPU_DENSE_CASE_TILES="1")
    adaptive = dict(tiled, GAGP_GPU_VIEW_PHYSICAL_BLOCK="0", GAGP_GPU_CASE_VIEW_BLOCK="1")
    return {"baseline": {}, "ordinary": ordinary,
            "untiled_dense": dict(ordinary, GAGP_GPU_TILED_INITIAL_PAYLOAD="0", GAGP_GPU_DENSE_CASE_LANES="1"),
            "dense_tiles": tiled, "dense_tiles_view": adaptive,
            "dense_tiles_view128": dict(adaptive, GAGP_GPU_VIEW_BOUND128="1")}


def matrix(name, tasks, arms, protocol, build, harness):
    with serial.GPU_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        stages = {v: serial.Stage(name + "-" + v, protocol["baseline_binary"] if v == "baseline" else build["binary"],
                                  settings=protocol["baseline_settings"]) for v in arms}
        for variant, stage in stages.items():
            stage.m.update(source_build="baseline" if variant == "baseline" else build["name"], harness_sha256=harness)
            stage.persist()
    rows = []
    for index, task in enumerate(tasks):
        assert time.time() < protocol["development_cutoff_epoch"], "Keep incomplete old-training matrix"
        order = list(arms)
        offset = index % len(order)
        for variant in order[offset:] + order[:offset]:
            development_v2.record_search(stages[variant], task, variant, protocol["development_inputs"][task], arms[variant])
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            compared = settings_followup_v2.compare({v: s.m["jobs"][f"{task}.s0.g64"] for v, s in stages.items()})
            for row in compared:
                job = stages[row["variant"]].m["jobs"][f"{task}.s0.g64"]
                doc = serial.read(job["output"])
                row["dense_tile_generations"] = sum("-dense-case-tiles" in g["execution_profile"] and
                                                       "-dense-case-tiles-declined" not in g["execution_profile"] for g in doc["generations"])
                row["case_view_generations"] = sum("-case-view-block" in g["execution_profile"] and
                                                     "-case-view-block-declined" not in g["execution_profile"] for g in doc["generations"])
            rows.extend(compared)
            serial.save(serial.RAW / (name + "-all-comparisons.json"), rows)
            if "candidate" in stages:
                serial.save(serial.RAW / (name + "-comparisons.json"), [r for r in rows if r["variant"] == "candidate"])
            assert all(r[k] for r in compared for k in ("full_final_ast_equal", "final_descriptors_equal", "champions_equal")), "Execution layout changed trajectory"
    means = {v: math.exp(sum(math.log(r["speedup"]) for r in rows if r["variant"] == v) / len(tasks))
             for v in arms if v != "baseline"}
    for stage in stages.values():
        stage.m.update(status="requested_phase_complete", finished_epoch=time.time())
        stage.persist()
    return means, rows


def main(name, build_name, apply_name):
    record = serial.RAW / (name + "-controller.json")
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    assert not record.exists()
    protocol = serial.read(serial.BASE / "protocol.json")
    state = dict(pid=os.getpid(), status="waiting_for_native_checks", started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 build_name=build_name, apply_name=apply_name,
                 harness_sha256={str(p): serial.sha(p) for p in
                                 [Path(__file__), serial.BASE / "serial.py", serial.BASE / "development_v2.py",
                                  serial.BASE / "settings_followup_v2.py", serial.BASE / "protocol.json"]})
    serial.save(record, state)
    try:
        while True:
            path = serial.RAW / build_name / "manifest.json"
            build = serial.read(path) if path.exists() else {}
            ap = serial.RAW / (apply_name + "-controller.json")
            applying = serial.read(ap) if ap.exists() else {}
            if build.get("status") == "failed" or applying.get("status") == "stopped":
                raise RuntimeError("Native/source prerequisite stopped; no candidate samples")
            if build.get("status") == "ok" and not Path(f"/proc/{build['pid']}").exists():
                break
            assert time.time() < protocol["development_cutoff_epoch"]
            time.sleep(10)
        for path, digest in state["harness_sha256"].items():
            assert serial.sha(path) == digest
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert all(serial.sha(serial.ROOT / p) == digest for p, digest in build["source_sha256"].items())
            assert serial.sha(build["binary"]) == build["binary_sha256"]
        previous = serial.read(serial.RAW / "dense3-controller.json")
        assert previous["status"] == "requested_phase_complete"
        arms = recipes(previous["existing_extra"], protocol["baseline_settings"]["GAGP_GPU_TILED_INITIAL_PAYLOAD"])
        build["name"] = build_name
        state.update(status="coarse_g64", binary=build["binary"], binary_sha256=build["binary_sha256"], source_build=build_name, arms=arms)
        serial.save(record, state)
        means, rows = matrix(name + "-coarse", development_v2.SCREEN_TASKS, arms, protocol, build, state["harness_sha256"])
        winner = max(means, key=means.get)
        # Prefer the simpler global view rule unless the narrower compilation
        # also clears the declared2% incremental performance gate.
        if winner == "dense_tiles_view128" and means[winner] / means["dense_tiles_view"] < 1.02:
            winner = "dense_tiles_view"
        admitted = means[winner] >= 1.02 and means[winner] / means["ordinary"] >= 1.02
        state.update(coarse_geometric_means=means, coarse_winner=winner, coarse_performance_admitted=admitted)
        serial.save(record, state)
        if admitted:
            state.update(status="full30_g64", candidate_extra=arms[winner])
            serial.save(record, state)
            full, full_rows = matrix(name, protocol["tasks"], {"baseline": {}, "ordinary": arms["ordinary"], "candidate": arms[winner]},
                                     protocol, build, state["harness_sha256"])
            state.update(task_count=30, geometric_mean=full["candidate"], geometric_means=full,
                         performance_admitted=full["candidate"] >= 1.02 and full["candidate"] / full["ordinary"] >= 1.02,
                         target_observed_in_development=full["candidate"] >= 1.3)
        else:
            state.update(task_count=0, performance_admitted=False)
        state.update(status="requested_phase_complete", finished_epoch=time.time(),
                     caveat="Old TRAINING layout development; not formal or new heldout quality")
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
    parser.add_argument("--build-name", required=True)
    parser.add_argument("--apply-name", required=True)
    args = parser.parse_args()
    main(args.name, args.build_name, args.apply_name)
