"""Independent all30 P16384/G64/R1 controls; retain all preparation/search/audit failures."""
from argparse import ArgumentParser
import fcntl
import math
import os
from pathlib import Path
import time
import traceback

import budgeted_audits
import serial
import speed


POPULATION = 16384


def validate(document, mode, count):
    assert document["population"] == len(document["final_population"]) == POPULATION
    assert document["cases"] == count and len(document["generations"]) == 64
    assert math.isfinite(document["evolve_wall_ms"]) and document["evolve_wall_ms"] > 0
    assert all(r["expected_program_cases"] == POPULATION * count for r in document["generations"])
    if mode == "asgp_1t":
        assert document["mode"] == mode and document["reproduction_profile"] == "asgp-fixed-native-elite1"
        assert math.isfinite(document["final_best"])
        return
    assert mode == "gpu_overlap"
    assert document["mode"] == mode
    assert document["reproduction_profile"].startswith("native-gpu-phase-") and not document["reproduction_fallback_reason"]
    assert document["verification_top_k"] == document["source_depth_cap"] == document["gpu_phase_site_depth"] == 0
    assert document["gpu_phase_site_parsimony"] == 0
    assert document["gpu_blocksize"] == document["cpu_audit_reduction_lanes"] == 512
    assert document["cpu_audit_program_count"] == document["cpu_audit_wrong_program_count"] == 16
    assert document["cpu_audit_mismatches"] == document["cpu_audit_wrong_mismatches"] == 0
    assert max(r["nodes"] for r in document["final_population"]) <= 2048
    assert all(r["cases"] == POPULATION * count and r["unscored"] == 0 and r["repro_kernel_ms"] > 0
               for r in document["generations"])


def compare(jobs):
    assert set(jobs) == {"baseline", "candidate", "asgp"}
    assert all(j["population"] == POPULATION and j["generations"] == 64 and j["repetition"] == 0 for j in jobs.values())
    row = speed.compare(jobs)
    row.update(population=POPULATION, final_ast_scope="all16384")
    return row


def prepare_task(stage, task, primary, freeze):
    seed = freeze["seed"]
    entry = dict(task=task, status="preparing")
    if primary.get("status") != "prepared" or primary.get("seeds", {}).get(str(seed), {}).get("status") != "ok":
        entry["status"] = "not_run_primary_input_failure"
        return entry
    frozen, source, prepared = [stage.out / (f"{task}.s{seed}.p16384" + suffix)
                                for suffix in (".frozen.json", ".source.json", ".prepared.json")]
    job = stage.run(f"{task}.p16384.freeze", ["freeze", task, frozen],
                    dict(GAGP_FREEZE_POPULATION=str(POPULATION), GAGP_FREEZE_POPULATION_SEED=str(seed)),
                    timeout=1800, reserve=freeze["reserve_s"])
    entry.update(status=job["status"], frozen_source=str(frozen))
    if job["status"] != "ok":
        return entry
    with serial.GPU_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if time.time() >= serial.DEADLINE - freeze["reserve_s"]:
            entry.update(status="budget_not_run", inputs_not_read=True)
            return entry
        original = primary["seeds"][str(seed)]
        assert serial.sha(original["source"]) == original["source_sha256"]
        document, reference = serial.read(frozen), serial.read(original["source"])
        assert document["task"] == task and document["population_seed"] == seed
        assert len(document["population"]) == POPULATION
        entry["primary_prefix_equal"] = document["population"][:1024] == reference["population"]
        if not entry["primary_prefix_equal"]:
            entry["prefix_note"] = "Different cross-population prefix; retain this original P16384 draw for all methods, no resampling"
        document.update(cases=reference["cases"], data_seed=freeze["data_seed"], quality_data_seed=freeze["data_seed"],
                        quality_split="train", study_scope="secondary P16384; same fresh split and one shared retained initial population for all methods")
        serial.save(source, document)
        entry.update(source=str(source), source_sha256=serial.sha(source), frozen_source_sha256=serial.sha(frozen),
                     grammar=primary["grammar"], grammar_sha256=primary["grammar_sha256"],
                     cases=primary["metadata"]["train_count"])
    job = stage.run(f"{task}.p16384.prepare", ["prepare", source, primary["grammar"], POPULATION, prepared],
                    timeout=1800, reserve=freeze["reserve_s"])
    with serial.GPU_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        entry.update(status=job["status"], prepared=str(prepared))
        if job["status"] == "ok":
            document = serial.read(prepared)
            entry["prepared_sha256"] = serial.sha(prepared)
            if len(document["programs"]) != POPULATION or document.get("admission_rejections"):
                entry["status"] = job["status"] = "native_unadmitted"
                stage.persist()
    return entry


def search(stage, task, entry, freeze, variant):
    seed = freeze["seed"]
    spec = freeze["variants"][variant]
    tag = f"{task}.s{seed}.r0.{variant}"
    metadata = dict(task=task, seed=seed, repetition=0, variant=variant, population=POPULATION,
                    generations=64, mode=spec["mode"])
    usable = entry.get("source") and (entry["status"] == "ok" or
                 (variant == "asgp" and entry["status"] == "native_unadmitted"))
    if not usable:
        assert tag not in stage.m["jobs"]
        job = dict(status="budget_not_run" if entry["status"] == "budget_not_run" else "not_run_input_preparation_failure",
                   preparation_status=entry["status"], **metadata)
        stage.m["jobs"][tag] = job
        stage.persist()
        return job
    count = entry["cases"]
    output, export, history = [stage.out / (tag + suffix) for suffix in (".json", ".export.json", ".history.json")]
    extra = dict(spec["extra"], GAGP_SEARCH_GENERATIONS="64", GAGP_BM_SEED=str(seed), GAGP_BM_CASE_COUNT=str(count),
                 GAGP_SEARCH_EXPORT=str(export), GAGP_SEARCH_HISTORY_EXPORT=str(history))
    assert "GAGP_SEARCH_EXPORT_BEST_ONLY" not in extra
    job = stage.run(tag, ["measure", entry["source"], entry["prepared"] if entry["status"] == "ok" else entry["source"],
                         entry["grammar"], POPULATION, "search_" + spec["mode"], output],
                    extra, timeout=3600, reserve=freeze["reserve_s"])
    with serial.GPU_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        job.update(**metadata, cases=count, output=str(output), export=str(export), history=str(history))
        if job["status"] == "ok":
            try:
                data = serial.read(output)
                validate(data, spec["mode"], count)
                exported = serial.read(export)
                if variant == "asgp":
                    assert "best_program" in exported
                    job["exported_program_count"] = 1
                else:
                    assert len(exported["programs"]) == POPULATION
                    job["exported_program_count"] = POPULATION
                assert len(serial.read(history)["history"]) == 64
                job["search_s"] = data["evolve_wall_ms"] / 1000
                for field, path in (("output", output), ("export", export), ("history", history)):
                    job[field + "_sha256"] = serial.sha(path)
            except Exception as error:
                job.update(status="invalid_output", error=repr(error))
        stage.persist()
    return job


def main(freeze_path, input_manifest, name):
    freeze = serial.read(freeze_path)
    protocol = serial.read(serial.BASE / "protocol.json")
    assert freeze["format"] == "gagp-g64-secondary-freeze-v1"
    assert freeze["started_epoch"] == serial.START and freeze["deadline_epoch"] == serial.DEADLINE
    assert freeze["tasks"] == protocol["tasks"] and freeze["population"] == POPULATION
    assert freeze["generations"] == 64 and freeze["repetitions"] == 1 and freeze["seed"] >= 35
    assert freeze["data_seed"] == 20261024 and freeze["reserve_s"] >= 14400
    assert set(freeze["variants"]) == {"baseline", "candidate", "asgp"}
    for path, digest in freeze["harness_sha256"].items():
        assert serial.sha(path) == digest
    for variant, spec in freeze["variants"].items():
        assert serial.sha(spec["binary"]) == spec["binary_sha256"]
        assert spec["mode"] == ("asgp_1t" if variant == "asgp" else "gpu_overlap")
        settings = dict(freeze["settings"], **spec["extra"])
        assert settings["GAGP_HOST_THREADS"] == "1" and settings["GAGP_GPU_PAYLOAD_WORKSPACE_MIB"] == "16384"
    prepared = serial.read(input_manifest)
    assert prepared["status"] == "requested_phase_complete" and prepared["freeze_sha256"] == freeze["quality_freeze_sha256"]
    assert set(prepared["inputs"]) == set(freeze["tasks"])
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists()
    state = dict(pid=os.getpid(), status="initializing", started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 freeze=str(freeze_path), freeze_sha256=serial.sha(freeze_path), input_manifest=str(input_manifest),
                 input_manifest_sha256=serial.sha(input_manifest), inputs={}, expected_searches=90,
                 expected_audits=420, expected_tasks=freeze["tasks"])
    serial.save(record, state)
    try:
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            admission = serial.Stage(name + "-inputs", freeze["variants"]["candidate"]["binary"], settings=freeze["settings"])
            searches = {v: serial.Stage(name + "-" + v, s["binary"], settings=freeze["settings"]) for v, s in freeze["variants"].items()}
            audits = {v: serial.Stage(name + "-" + v + "-audits", s["binary"], settings=freeze["settings"]) for v, s in freeze["variants"].items()}
            for stage in (admission, *searches.values(), *audits.values()):
                stage.m.update(freeze=str(freeze_path), freeze_sha256=state["freeze_sha256"])
                stage.persist()
        state["status"] = "prepare_search_and_audit"
        serial.save(record, state)
        comparisons = []
        for index, task in enumerate(freeze["tasks"]):
            state["current_task"] = task
            primary = prepared["inputs"][task]
            entry = prepare_task(admission, task, primary, freeze)
            state["inputs"][task] = entry
            serial.save(record, state)
            order = list(searches)
            offset = index % 3
            jobs = {v: search(searches[v], task, entry, freeze, v) for v in order[offset:] + order[:offset]}
            with serial.GPU_LOCK.open("a") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                comparisons.append(compare(jobs))
                serial.save(serial.RAW / (name + "-comparisons.json"), comparisons)
            audit_inputs = dict(primary, seeds={str(freeze["seed"]): dict(source=entry.get("source"), prepared=entry.get("prepared"))})
            for variant, job in jobs.items():
                stage = audits[variant]
                if variant != "asgp":
                    full_inputs = dict(source=entry.get("source"), grammar=primary.get("grammar"),
                                       prefixes={str(POPULATION): dict(prepared=entry.get("prepared"))})
                    budgeted_audits.full(stage, job, full_inputs, freeze)
                for split in ("train", "test"):
                    budgeted_audits.common(stage, job, split, audit_inputs, freeze)
                    (budgeted_audits.asgp if variant == "asgp" else budgeted_audits.run)(stage, job, split, audit_inputs, freeze)
        for stage in (admission, *searches.values(), *audits.values()):
            stage.m.update(status="requested_phase_complete", finished_epoch=time.time())
            stage.persist()
        state.update(status="requested_phase_complete", finished_epoch=time.time(), recorded_tasks=len(comparisons),
                     caveat="Secondary all30 planned, one repetition; actual statuses/fidelity determine coverage, not phase completion")
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
