"""Frozen old-TRAINING G64 calibration and execution-setting screen."""
from argparse import ArgumentParser
import fcntl
import math
import os
from pathlib import Path
import time
import traceback

import serial


SCREEN_TASKS = ["sum_of_elements", "is_sorted", "filter_odd", "double_letters",
                "median", "house_robber", "tribonacci", "grade"]
ARMS = {
    "baseline": {},
    "view128": {"GAGP_GPU_VIEW_PHYSICAL_BLOCK": "128"},
    "view256": {"GAGP_GPU_VIEW_PHYSICAL_BLOCK": "256"},
    "no_shape": {"GAGP_GPU_SHAPE_RESULT_CACHE": "0"},
    "no_parent": {"GAGP_GPU_PARENT_FITNESS_CACHE": "0"},
    "pool": {"GAGP_GPU_CUDA_POOL": "1"},
}


def validate(document, cases):
    assert document["population"] == 1024 and document["cases"] == cases
    assert len(document["generations"]) == 64
    assert len(document["final_population"]) == 1024
    assert document["reproduction_profile"].startswith("native-gpu-phase-")
    assert not document["reproduction_fallback_reason"]
    assert document["verification_top_k"] == 0
    assert document["source_depth_cap"] == document["gpu_phase_site_depth"] == 0
    assert document["gpu_phase_site_parsimony"] == 0
    assert document["gpu_blocksize"] == document["cpu_audit_reduction_lanes"] == 512
    assert document["cpu_audit_program_count"] == document["cpu_audit_wrong_program_count"] == 16
    assert document["cpu_audit_mismatches"] == document["cpu_audit_wrong_mismatches"] == 0
    assert max(r["nodes"] for r in document["final_population"]) <= 2048
    assert math.isfinite(document["evolve_wall_ms"]) and document["evolve_wall_ms"] > 0
    for row in document["generations"]:
        assert row["cases"] == row["expected_program_cases"] == 1024 * cases
        assert row["unscored"] == 0 and row["repro_kernel_ms"] > 0


def record_search(stage, task, variant, spec, settings):
    tag = f"{task}.s0.g64"
    output, export, history = [stage.out / (tag + suffix) for suffix in
                               (".json", ".export.json", ".history.json")]
    extra = dict(settings, GAGP_SEARCH_GENERATIONS="64", GAGP_BM_SEED="0",
                 GAGP_BM_CASE_COUNT=str(spec["train_count"]),
                 GAGP_SEARCH_EXPORT=str(export), GAGP_SEARCH_HISTORY_EXPORT=str(history))
    job = stage.run(tag, ["measure", spec["source"], spec["prepared"], spec["grammar"],
                         1024, "search_gpu_overlap", output], extra, timeout=1800, reserve=14400)
    with serial.GPU_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        job.update(task=task, variant=variant, population=1024, generations=64,
                   cases=spec["train_count"], seed=0, output=str(output),
                   export=str(export), history=str(history))
        if job["status"] == "ok":
            try:
                doc = serial.read(output)
                validate(doc, spec["train_count"])
                exported = serial.read(export)
                assert len(exported["programs"]) == 1024, "Full export requires every final AST; best-only is not full-population proof"
                job["exported_program_count"] = len(exported["programs"])
                job.update(search_s=doc["evolve_wall_ms"] / 1000,
                           components_s={key: sum(r.get(key, 0) for r in doc["generations"]) / 1000
                                         for key in ("eval_ms", "eval_kernel_ms", "repro_ms", "repro_kernel_ms",
                                                     "pack_ms", "upload_ms", "initial_kernel_wall_ms", "replay_kernel_event_ms")},
                           initial_admission_s=doc["initial_admission_ms"] / 1000,
                           gpu_init_s=doc["gpu_init_ms"] / 1000,
                           final_eval_s=doc["final_eval_ms"] / 1000)
                for key, path in [("output", output), ("export", export), ("history", history)]:
                    job[key + "_sha256"] = serial.sha(path)
            except Exception as error:
                job.update(status="invalid_output", error=repr(error))
        stage.persist()
    if job["status"] != "ok":
        raise RuntimeError(f"Preserved {task}/{variant} failure: {job['status']}")
    return job


def main(name, phase):
    protocol = serial.read(serial.BASE / "protocol.json")
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    if record.exists():
        raise RuntimeError("Existing attempt is immutable; inspect it before explicit recovery")
    state = dict(pid=os.getpid(), status="initializing", phase=phase,
                 started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 scope="old TRAINING only; no fresh quality labels",
                 harness_sha256={str(p): serial.sha(p) for p in
                                 [Path(__file__), serial.BASE / "serial.py", serial.BASE / "protocol.json"]})
    serial.save(record, state)
    variants = {"baseline": {}} if phase == "calibration" else ARMS
    tasks = protocol["tasks"] if phase == "calibration" else SCREEN_TASKS
    stages = {}
    comparisons = []
    try:
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert serial.sha(protocol["baseline_binary"]) == protocol["baseline_binary_sha256"]
            for task in tasks:
                for path, digest in protocol["development_inputs"][task]["sha256"].items():
                    assert serial.sha(path) == digest, "Development input changed"
            stages = {v: serial.Stage(name + "-" + v, protocol["baseline_binary"],
                                      settings=protocol["baseline_settings"]) for v in variants}
            for stage in stages.values():
                stage.m["harness_sha256"] = state["harness_sha256"]
                stage.persist()
        state["status"] = "running"
        serial.save(record, state)
        for index, task in enumerate(tasks):
            if time.time() >= protocol["development_cutoff_epoch"]:
                raise RuntimeError("Development cutoff reached; no clock reset")
            order = list(variants)
            order = order[index % len(order):] + order[:index % len(order)]
            for variant in order:
                record_search(stages[variant], task, variant,
                              protocol["development_inputs"][task], variants[variant])
            if len(variants) == 1:
                continue
            with serial.GPU_LOCK.open("a") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                tag = f"{task}.s0.g64"
                baseline = stages["baseline"].m["jobs"][tag]
                a = serial.read(baseline["output"])
                history_a = serial.read(baseline["history"])["history"]
                for variant in variants:
                    if variant == "baseline":
                        continue
                    current = stages[variant].m["jobs"][tag]
                    b = serial.read(current["output"])
                    history_b = serial.read(current["history"])["history"]
                    row = dict(task=task, variant=variant, speedup=baseline["search_s"] / current["search_s"],
                               final_descriptors_equal=a["final_population"] == b["final_population"],
                               full_final_ast_equal=baseline["export_sha256"] == current["export_sha256"],
                               champions_equal=[(r["ast"], r["fitness"]) for r in history_a] ==
                                               [(r["ast"], r["fitness"]) for r in history_b])
                    comparisons.append(row)
                    serial.save(serial.RAW / (name + "-comparisons.json"), comparisons)
                    assert all(row[k] for k in ("final_descriptors_equal", "full_final_ast_equal", "champions_equal")), "Execution trajectory mismatch"
        means = {variant: math.exp(sum(math.log(r["speedup"]) for r in comparisons if r["variant"] == variant) / len(tasks))
                 for variant in variants if variant != "baseline"}
        state.update(status="requested_phase_complete", finished_epoch=time.time(),
                     task_count=len(tasks), geometric_means=means,
                     caveat="Screen selection only; not all30 validation, ASGP speedup or quality equivalence")
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
    parser.add_argument("--phase", choices=["calibration", "screen"], required=True)
    args = parser.parse_args()
    main(args.name, args.phase)
