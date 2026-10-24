"""G64-only old-TRAINING execution settings, then one global full30 winner."""
from argparse import ArgumentParser
import fcntl
import math
import os
from pathlib import Path
import time
import traceback

import development
import serial


ARMS = {
    "baseline": {},
    "view128": {"GAGP_GPU_VIEW_PHYSICAL_BLOCK": "128"},
    "view128_reg512": {"GAGP_GPU_VIEW_PHYSICAL_BLOCK": "128", "GAGP_GPU_RELAXED_VIEW_REGISTERS": "1"},
    "view128_stack": {"GAGP_GPU_VIEW_PHYSICAL_BLOCK": "128", "GAGP_GPU_REGISTER_PHASE_STACK": "1"},
    "view128_threads8": {"GAGP_GPU_VIEW_PHYSICAL_BLOCK": "128", "GAGP_PHASE_REPRO_THREADS": "8"},
    "view128_threads16": {"GAGP_GPU_VIEW_PHYSICAL_BLOCK": "128", "GAGP_PHASE_REPRO_THREADS": "16"},
    "view128_threads32": {"GAGP_GPU_VIEW_PHYSICAL_BLOCK": "128", "GAGP_PHASE_REPRO_THREADS": "32"},
}


def compare(jobs):
    docs = {v: serial.read(j["output"]) for v, j in jobs.items()}
    histories = {v: [(r["ast"], r["fitness"]) for r in serial.read(j["history"])["history"]]
                 for v, j in jobs.items()}
    rows = []
    for variant in jobs:
        if variant == "baseline":
            continue
        row = dict(task=jobs[variant]["task"], variant=variant,
                   speedup=jobs["baseline"]["search_s"] / jobs[variant]["search_s"],
                   final_descriptors_equal=docs["baseline"]["final_population"] == docs[variant]["final_population"],
                   full_final_ast_equal=jobs["baseline"]["export_sha256"] == jobs[variant]["export_sha256"],
                   champions_equal=histories["baseline"] == histories[variant])
        rows.append(row)
    return rows


def matrix(name, tasks, arms, protocol):
    with serial.GPU_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        stages = {v: serial.Stage(name + "-" + v, protocol["baseline_binary"],
                                  settings=protocol["baseline_settings"]) for v in arms}
        for stage in stages.values():
            stage.m.update(harness_sha256={str(p): serial.sha(p) for p in
                           [Path(__file__), serial.BASE / "serial.py", serial.BASE / "development.py", serial.BASE / "protocol.json"]})
            stage.persist()
    comparisons = []
    for index, task in enumerate(tasks):
        if time.time() >= protocol["development_cutoff_epoch"]:
            raise RuntimeError("Development cutoff; preserve all completed and failed samples")
        variants = list(arms)
        variants = variants[index % len(variants):] + variants[:index % len(variants)]
        for variant in variants:
            development.record_search(stages[variant], task, variant, protocol["development_inputs"][task], arms[variant])
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            rows = compare({v: stages[v].m["jobs"][f"{task}.s0.g64"] for v in arms})
            comparisons.extend(rows)
            serial.save(serial.RAW / (name + "-comparisons.json"), comparisons)
            assert all(r[k] for r in rows for k in ("final_descriptors_equal", "full_final_ast_equal", "champions_equal")), "Execution-setting trajectory mismatch"
    for stage in stages.values():
        stage.m.update(status="requested_phase_complete", finished_epoch=time.time())
        stage.persist()
    return {v: math.exp(sum(math.log(r["speedup"]) for r in comparisons if r["variant"] == v) / len(tasks))
            for v in arms if v != "baseline"}


def main(name, wait_controller):
    protocol = serial.read(serial.BASE / "protocol.json")
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists(), "Keep previous attempts"
    state = dict(pid=os.getpid(), status="waiting_for_prior", started_epoch=time.time(),
                 deadline_epoch=serial.DEADLINE, waiting_for=wait_controller,
                 scope="old TRAINING runtime/parity only, no new heldout",
                 harness_sha256={str(p): serial.sha(p) for p in
                                 [Path(__file__), serial.BASE / "serial.py", serial.BASE / "development.py", serial.BASE / "protocol.json"]})
    serial.save(record, state)
    try:
        while True:
            path = serial.RAW / (wait_controller + "-controller.json")
            previous = serial.read(path) if path.exists() else {}
            if previous.get("status") in ("requested_phase_complete", "stopped") and not Path(f"/proc/{previous['pid']}").exists():
                break
            if time.time() >= protocol["development_cutoff_epoch"]:
                raise RuntimeError("Prior work missed development cutoff")
            time.sleep(10)
        for path, digest in state["harness_sha256"].items():
            assert serial.sha(path) == digest
        state["status"] = "coarse_g64"
        serial.save(record, state)
        means = matrix(name + "-coarse", development.SCREEN_TASKS, ARMS, protocol)
        winner = max(means, key=means.get)
        state.update(coarse_geometric_means=means, coarse_winner=winner,
                     selection_rule="one global highest complete 8-task mean, >=2% vs fresh baseline; full30 fresh control required")
        serial.save(record, state)
        if means[winner] >= 1.02:
            state["status"] = "full30_g64"
            serial.save(record, state)
            full = matrix(name + "-full", protocol["tasks"],
                          {"baseline": {}, "candidate": ARMS[winner]}, protocol)
            state.update(full30_geometric_means=full, selected_extra=ARMS[winner],
                         performance_admitted=full["candidate"] >= 1.02)
        else:
            state.update(performance_admitted=False, selected_extra={})
        state.update(status="requested_phase_complete", finished_epoch=time.time(),
                     caveat="Development/selection evidence, not independent quality or ASGP evidence")
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--wait-controller", required=True)
    args = parser.parse_args()
    main(args.name, args.wait_controller)
