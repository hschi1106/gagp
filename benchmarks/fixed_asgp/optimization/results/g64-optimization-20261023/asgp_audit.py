"""Offline ASGP-native fitness/wrong for every selected generation and final."""
import fcntl
import math

import champion_audit
import serial


def validate(rows, search, split, count):
    assert len(rows) == len(search["generations"]) + 1
    assert all(r["cases"] == count and math.isfinite(r["fitness"]) and r["nodes"] > 0
               and type(r["wrong"]) is int and 0 <= r["wrong"] <= count for r in rows)
    mismatches = dict(search_native_fitness=0, search_native_wrong=0, final_descriptor=0)
    if split == "train":
        for native, generation in zip(rows[:-1], search["generations"]):
            mismatches["search_native_fitness"] += native["fitness"] != generation["best"]
            mismatches["search_native_wrong"] += native["wrong"] != generation["best_wrong"]
        final = rows[-1]
        mismatches["search_native_fitness"] += final["fitness"] != search["final_best"]
        mismatches["final_descriptor"] += not any(
            all(final[field] == member[field] for field in ("fitness", "wrong", "nodes"))
            for member in search["final_population"])
    return dict(native_fidelity_pass=not any(mismatches.values()), native_mismatches=mismatches,
                native_program_observations=len(rows), native_case_observations=len(rows) * count,
                scope="ASGP original CPU Score fitness/wrong, not common exact-output loss or GPU parity")


def run(stage, search_job, split, inputs, freeze):
    task, seed, variant = (search_job[k] for k in ("task", "seed", "variant"))
    tag = f"{task}.s{seed}.r{search_job['repetition']}.{variant}.{split}.native"
    meta = dict(task=task, seed=seed, variant=variant, repetition=search_job["repetition"], split=split,
                diagnostic="ASGP-native CPU Score; all selected generation and final programs")
    if search_job["status"] != "ok":
        assert tag not in stage.m["jobs"]
        stage.m["jobs"][tag] = dict(status="not_run_search_failure", **meta)
        stage.persist()
        return stage.m["jobs"][tag]
    with serial.GPU_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        for field in ("output", "export", "history"):
            assert serial.sha(search_job[field]) == search_job[field + "_sha256"]
        search = serial.read(search_job["output"])
        history = serial.read(search_job["history"])["history"]
        observations = [r["program"] for r in history] + [serial.read(search_job["export"])["best_program"]]
        unique, mapping, digests = champion_audit.compress_programs(observations)
        source = dict(serial.read(inputs["seeds"][str(seed)]["source"]))
        source["population"] = unique
        if split == "test":
            source["cases"] = serial.read(inputs["test_source"])["cases"]
        count = inputs["metadata"][split + "_count"]
        source_path, raw_path, expanded_path, mapping_path = [stage.out / (tag + suffix) for suffix in
            (".source.json", ".raw.json", ".json", ".mapping.json")]
        assert not any(p.exists() for p in (source_path, raw_path, expanded_path, mapping_path))
        serial.save(source_path, source)
        serial.save(mapping_path, dict(observation_to_unique=mapping, unique_ast_sha256=digests,
                                      native_cpu_executed_programs=len(unique), logical_program_observations=len(mapping)))
    job = stage.run(tag, ["measure", source_path, source_path, inputs["grammar"], len(unique),
                          "asgp_snapshot", raw_path], {"GAGP_BM_CASE_COUNT": str(count)},
                    timeout=2400, reserve=freeze["reserve_s"])
    with serial.GPU_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        job.update(**meta, source=str(source_path), raw_output=str(raw_path), output=str(expanded_path),
                   mapping=str(mapping_path), cases=count, native_cpu_executed_programs=len(unique),
                   native_cpu_executed_cases=len(unique) * count)
        if job["status"] == "ok":
            try:
                raw = serial.read(raw_path)["programs"]
                assert len(raw) == len(unique)
                rows = [dict(raw[index], program=position, source_unique_program=index)
                        for position, index in enumerate(mapping)]
                result = validate(rows, search, split, count)
                serial.save(expanded_path, dict(programs=rows, **result))
                job.update(**result)
                for field, path in [("output", expanded_path), ("raw_output", raw_path), ("mapping", mapping_path)]:
                    job[field + "_sha256"] = serial.sha(path)
                if not job["native_fidelity_pass"]:
                    job["status"] = "native_fidelity_mismatch"
            except Exception as error:
                job.update(status="invalid_output", error=repr(error))
        stage.persist()
    return job
