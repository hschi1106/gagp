"""All30 G64 fixed-work controls: current GPU, candidate GPU and ASGP1T."""
from argparse import ArgumentParser
import fcntl
import math
import os
from pathlib import Path
import time
import traceback

import asgp_audit
import development_v2
import serial


def validate(document, mode, cases):
    if mode == "gpu_overlap":
        development_v2.validate(document, cases)
        return
    assert mode == document["mode"] == "asgp_1t"
    assert document["population"] == len(document["final_population"]) == 1024
    assert document["cases"] == cases and len(document["generations"]) == 64
    assert document["reproduction_profile"] == "asgp-fixed-native-elite1"
    assert math.isfinite(document["evolve_wall_ms"]) and document["evolve_wall_ms"] > 0
    assert math.isfinite(document["final_best"])
    assert all(r["expected_program_cases"] == 1024 * cases for r in document["generations"])


def search(stage, freeze, task, repetition, variant, inputs):
    seed = freeze["seed"]
    spec = freeze["variants"][variant]
    mode = spec["mode"]
    tag = f"{task}.s{seed}.r{repetition}.{variant}"
    entry = inputs.get("seeds", {}).get(str(seed), {})
    meta = dict(task=task, seed=seed, repetition=repetition, variant=variant,
                population=1024, generations=64, mode=mode)
    if inputs.get("status") != "prepared" or not entry.get("source") or (mode != "asgp_1t" and entry.get("status") != "ok"):
        assert tag not in stage.m["jobs"]
        stage.m["jobs"][tag] = dict(status="not_run_input_preparation_failure", **meta)
        stage.persist()
        return stage.m["jobs"][tag]
    count = inputs["metadata"]["train_count"]
    output, export, history = [stage.out / (tag + suffix) for suffix in
                               (".json", ".export.json", ".history.json")]
    extra = dict(spec["extra"], GAGP_SEARCH_GENERATIONS="64", GAGP_BM_SEED=str(seed),
                 GAGP_BM_CASE_COUNT=str(count), GAGP_SEARCH_EXPORT=str(export),
                 GAGP_SEARCH_HISTORY_EXPORT=str(history))
    # The CLI checks presence, so omit this variable for a full R0 export.
    if repetition > 0:
        extra["GAGP_SEARCH_EXPORT_BEST_ONLY"] = "1"
    prepared = entry["prepared"] if entry.get("status") == "ok" else entry["source"]
    job = stage.run(tag, ["measure", entry["source"], prepared, inputs["grammar"],
                         1024, "search_" + mode, output], extra, timeout=3600, reserve=freeze["reserve_s"])
    with serial.GPU_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        job.update(**meta, cases=count, output=str(output), export=str(export), history=str(history))
        if job["status"] == "ok":
            try:
                doc = serial.read(output)
                validate(doc, mode, count)
                exported = serial.read(export)
                if mode == "gpu_overlap":
                    expected = 1024 if repetition == 0 else 1
                    assert len(exported["programs"]) == expected, "Wrong final AST export coverage"
                    job["exported_program_count"] = expected
                else:
                    assert "best_program" in exported
                    job["exported_program_count"] = 1
                assert len(serial.read(history)["history"]) == 64
                job["search_s"] = doc["evolve_wall_ms"] / 1000
                for field, path in [("output", output), ("export", export), ("history", history)]:
                    job[field + "_sha256"] = serial.sha(path)
            except Exception as error:
                job.update(status="invalid_output", error=repr(error))
        stage.persist()
    return job


def compare(jobs):
    baseline, candidate, asgp = (jobs[k] for k in ("baseline", "candidate", "asgp"))
    row = dict(task=baseline["task"], population=1024, repetition=baseline["repetition"],
               seed=baseline["seed"], cases=baseline.get("cases"), generations=64,
               statuses=[baseline["status"], candidate["status"]], asgp_status=asgp["status"],
               speedup=None, asgp_over_baseline=None, asgp_over_candidate=None,
               final_descriptors_equal=False, champion_ast_fitness_equal=False, final_export_equal=False,
               final_ast_scope="all1024" if baseline["repetition"] == 0 else "best-only")
    if baseline["status"] == candidate["status"] == "ok":
        a, b = (serial.read(j["output"]) for j in (baseline, candidate))
        histories = [[(r["ast"], r["fitness"]) for r in serial.read(j["history"])["history"]]
                     for j in (baseline, candidate)]
        row.update(speedup=baseline["search_s"] / candidate["search_s"],
                   final_descriptors_equal=a["final_population"] == b["final_population"],
                   champion_ast_fitness_equal=histories[0] == histories[1],
                   final_export_equal=baseline["export_sha256"] == candidate["export_sha256"])
        if asgp["status"] == "ok":
            row.update(asgp_over_baseline=asgp["search_s"] / baseline["search_s"],
                       asgp_over_candidate=asgp["search_s"] / candidate["search_s"])
    return row


def main(freeze_path, input_manifest, name):
    protocol = serial.read(serial.BASE / "protocol.json")
    freeze = serial.read(freeze_path)
    assert freeze["format"] == "gagp-g64-speed-freeze-v1"
    assert freeze["started_epoch"] == serial.START and freeze["deadline_epoch"] == serial.DEADLINE
    assert freeze["tasks"] == protocol["tasks"] and freeze["population"] == 1024
    assert freeze["generations"] == 64 and freeze["repetitions"] == 3 and freeze["seed"] >= 35
    assert freeze["reserve_s"] >= 14400 and set(freeze["variants"]) == {"baseline", "candidate", "asgp"}
    for path, digest in freeze["harness_sha256"].items():
        assert serial.sha(path) == digest
    for variant, spec in freeze["variants"].items():
        assert serial.sha(spec["binary"]) == spec["binary_sha256"]
        assert spec["mode"] == ("asgp_1t" if variant == "asgp" else "gpu_overlap")
        assert "GAGP_SEARCH_EXPORT_BEST_ONLY" not in spec["extra"]
    prepared = serial.read(input_manifest)
    assert prepared["status"] == "requested_phase_complete"
    assert prepared["freeze_sha256"] == freeze["quality_freeze_sha256"]
    inputs = prepared["inputs"]
    assert set(inputs) == set(freeze["tasks"])
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists(), "Existing attempt requires explicit verified recovery"
    state = dict(pid=os.getpid(), status="initializing", started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 freeze=str(freeze_path), freeze_sha256=serial.sha(freeze_path),
                 input_manifest=str(input_manifest), input_manifest_sha256=serial.sha(input_manifest))
    serial.save(record, state)
    stages = {}
    comparisons = []
    try:
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            stages = {v: serial.Stage(name + "-" + v, spec["binary"], settings=freeze["settings"])
                      for v, spec in freeze["variants"].items()}
            for variant, stage in stages.items():
                stage.m.update(freeze=str(freeze_path), freeze_sha256=state["freeze_sha256"],
                               expected_jobs=[f"{task}.s{freeze['seed']}.r{r}.{variant}"
                                              for r in range(3) for task in freeze["tasks"]])
                stage.persist()
        state["status"] = "searches"
        serial.save(record, state)
        for repetition in range(3):
            for index, task in enumerate(freeze["tasks"]):
                variants = list(stages)
                offset = (repetition + index) % len(variants)
                for variant in variants[offset:] + variants[:offset]:
                    search(stages[variant], freeze, task, repetition, variant, inputs[task])
                with serial.GPU_LOCK.open("a") as lock:
                    fcntl.flock(lock, fcntl.LOCK_EX)
                    jobs = {v: stage.m["jobs"][f"{task}.s{freeze['seed']}.r{repetition}.{v}"]
                            for v, stage in stages.items()}
                    comparisons.append(compare(jobs))
                    serial.save(serial.RAW / (name + "-comparisons.json"), comparisons)
        for stage in stages.values():
            stage.m.update(status="requested_phase_complete", finished_epoch=time.time())
            stage.persist()
        state.update(status="requested_phase_complete", finished_epoch=time.time(),
                     expected_searches=270, recorded_searches=sum(len(s.m["jobs"]) for s in stages.values()),
                     caveat="Search completion only; native audits, paired fidelity and full coverage separately qualify results")
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--freeze", type=Path, required=True)
    parser.add_argument("--input-manifest", type=Path, required=True)
    parser.add_argument("--name", required=True)
    args = parser.parse_args()
    main(args.freeze.resolve(), args.input_manifest.resolve(), args.name)
