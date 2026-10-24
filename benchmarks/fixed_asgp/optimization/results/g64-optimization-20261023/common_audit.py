"""Offline exact train/test outputs for GPU or ASGP training-selected champions."""
import fcntl
import math

import serial


def validate(document, count, task, generations, backend):
    assert document["task"] == task and document["cases"] == count
    assert len(document["history"]) == generations and len(document["cases_sha256"]) == 64
    for score in document["history"] + [document["final"]]:
        rows = score["case_results"]
        assert score["backend"] == backend and score["cases"] == len(rows) == count
        assert [row["case"] for row in rows] == list(range(count))
        assert all(row["exact"] in (0, 1) and row["runtime_error"] in (0, 1) for row in rows)
        correct = sum(row["exact"] for row in rows)
        assert score["correct_count"] == score["exact"] == correct
        assert score["wrong"] == count - correct
        assert score["runtime_errors"] == sum(row["runtime_error"] for row in rows)
        assert math.isclose(score["exact_accuracy"], correct / count, rel_tol=0, abs_tol=1e-12)


def run(stage, search_job, split, inputs, freeze):
    task, seed, variant = (search_job[k] for k in ("task", "seed", "variant"))
    tag = f"{task}.s{seed}.r{search_job['repetition']}.{variant}.{split}.common"
    meta = dict(task=task, seed=seed, variant=variant, repetition=search_job["repetition"], split=split)
    if search_job["status"] != "ok":
        assert tag not in stage.m["jobs"]
        stage.m["jobs"][tag] = dict(status="not_run_search_failure", **meta)
        stage.persist()
        return stage.m["jobs"][tag]
    for field in ("export", "history"):
        assert serial.sha(search_job[field]) == search_job[field + "_sha256"]
    source = inputs["seeds"][str(seed)]["source"] if split == "train" else inputs["test_source"]
    count = inputs["metadata"][split + "_count"]
    output = stage.out / (tag + ".json")
    job = stage.run(tag, ["quality-source", source, search_job["export"], inputs["grammar"], output,
                         search_job["history"]], {"GAGP_BM_CASE_COUNT": str(count)},
                    timeout=2400, reserve=freeze["reserve_s"])
    with serial.GPU_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        job.update(**meta, cases=count, output=str(output))
        if job["status"] == "ok":
            try:
                backend = "asgp_1t" if variant == "asgp" else "gagp_cpu_1t"
                validate(serial.read(output), count, task, 64, backend)
                job["output_sha256"] = serial.sha(output)
            except Exception as error:
                job.update(status="invalid_output", error=repr(error))
        stage.persist()
    return job
