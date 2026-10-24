"""Admit one native implementation using fresh old-TRAINING controls."""
from argparse import ArgumentParser
import fcntl
import math
import os
from pathlib import Path
import time
import traceback

import development_v2 as development
import serial


def run(name, build_name):
    record = serial.RAW / (name + "-controller.json")
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    assert not record.exists(), "Preserve existing attempts"
    protocol = serial.read(serial.BASE / "protocol.json")
    state = dict(pid=os.getpid(), status="waiting_for_native_checks", started_epoch=time.time(),
                 deadline_epoch=serial.DEADLINE, build_name=build_name,
                 harness_sha256={str(p): serial.sha(p) for p in
                                 [Path(__file__), serial.BASE / "serial.py", serial.BASE / "development_v2.py", serial.BASE / "protocol.json"]})
    serial.save(record, state)
    stages = {}
    comparisons = []
    try:
        while True:
            path = serial.RAW / build_name / "manifest.json"
            build = serial.read(path) if path.exists() else {}
            if build.get("status") == "failed":
                raise RuntimeError("Native prerequisite failed; no candidate search launched")
            if build.get("status") == "ok" and not Path(f"/proc/{build['pid']}").exists():
                break
            if time.time() >= protocol["development_cutoff_epoch"]:
                raise RuntimeError("Native prerequisite missed development cutoff")
            time.sleep(10)
        for path, digest in state["harness_sha256"].items():
            assert serial.sha(path) == digest, "Pilot harness changed after launch"
        assert serial.sha(build["binary"]) == build["binary_sha256"]
        variants = {"baseline": {},
                    "old_view128": {"GAGP_GPU_VIEW_PHYSICAL_BLOCK": "128"},
                    "ordinary": {"GAGP_GPU_VIEW_PHYSICAL_BLOCK": "128", "GAGP_GPU_VIEW_BOUND128": "0"},
                    "bound128": {"GAGP_GPU_VIEW_PHYSICAL_BLOCK": "128", "GAGP_GPU_VIEW_BOUND128": "1"}}
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert all(serial.sha(serial.ROOT / p) == h for p, h in build["source_sha256"].items())
            stages = {v: serial.Stage(name + "-" + v,
                      protocol["baseline_binary"] if v in ("baseline", "old_view128") else build["binary"],
                      settings=protocol["baseline_settings"]) for v in variants}
            for stage in stages.values():
                stage.m.update(harness_sha256=state["harness_sha256"], source_build=build_name)
                stage.persist()
        state.update(status="coarse_g64", tasks=development.SCREEN_TASKS,
                     binary=build["binary"], binary_sha256=build["binary_sha256"])
        serial.save(record, state)
        for index, task in enumerate(development.SCREEN_TASKS):
            order = list(variants)
            order = order[index % len(order):] + order[:index % len(order)]
            for variant in order:
                development.record_search(stages[variant], task, variant,
                                           protocol["development_inputs"][task], variants[variant])
            with serial.GPU_LOCK.open("a") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                tag = f"{task}.s0.g64"
                jobs = {v: stages[v].m["jobs"][tag] for v in variants}
                docs = {v: serial.read(j["output"]) for v, j in jobs.items()}
                histories = {v: [(r["ast"], r["fitness"]) for r in serial.read(j["history"])["history"]]
                             for v, j in jobs.items()}
                for variant in ("old_view128", "ordinary", "bound128"):
                    row = dict(task=task, variant=variant, speedup=jobs["baseline"]["search_s"] / jobs[variant]["search_s"],
                               full_final_ast_equal=jobs["baseline"]["export_sha256"] == jobs[variant]["export_sha256"],
                               final_descriptors_equal=docs["baseline"]["final_population"] == docs[variant]["final_population"],
                               champions_equal=histories["baseline"] == histories[variant],
                               bound_active_generations=sum("-view-bound128" in r["execution_profile"] and
                                                           "-view-bound128-declined" not in r["execution_profile"]
                                                           for r in docs[variant]["generations"]))
                    comparisons.append(row)
                    serial.save(serial.RAW / (name + "-comparisons.json"), comparisons)
                    assert all(row[k] for k in ("full_final_ast_equal", "final_descriptors_equal", "champions_equal")), "Execution-only trajectory mismatch"
        means = {variant: math.exp(sum(math.log(r["speedup"]) for r in comparisons if r["variant"] == variant) / len(development.SCREEN_TASKS))
                 for variant in ("old_view128", "ordinary", "bound128")}
        activated = sum(r["bound_active_generations"] for r in comparisons if r["variant"] == "bound128")
        admitted = activated > 0 and means["bound128"] >= 1.02 and means["bound128"] / max(means["ordinary"], means["old_view128"]) >= 1.02
        state.update(status="requested_phase_complete", finished_epoch=time.time(),
                     geometric_means=means, bound_over_ordinary=means["bound128"] / means["ordinary"],
                     bound_active_generations=activated, performance_admitted=admitted,
                     scope="8 old-TRAINING tasks; coarse screen only, never the all30 formal claim")
        for stage in stages.values():
            stage.m.update(status="requested_phase_complete", finished_epoch=time.time())
            stage.persist()
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
    run(args.name, args.build_name)
