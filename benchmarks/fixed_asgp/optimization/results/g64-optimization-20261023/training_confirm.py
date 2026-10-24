"""Confirm a complete global G64 recipe on two additional old initial seeds."""
from argparse import ArgumentParser
import fcntl
import math
import os
from pathlib import Path
import time
import traceback

import champion_audit
import development_v2
import serial
import settings_followup_v2


def search(stage, task, seed, variant, entry, grammar, cases, extra):
    tag = f"{task}.s{seed}.g64"
    output, export, history = [stage.out / (tag + suffix) for suffix in
                               (".json", ".export.json", ".history.json")]
    options = dict(extra, GAGP_SEARCH_GENERATIONS="64", GAGP_BM_SEED=str(seed), GAGP_BM_CASE_COUNT=str(cases),
                   GAGP_SEARCH_EXPORT=str(export), GAGP_SEARCH_HISTORY_EXPORT=str(history))
    assert "GAGP_SEARCH_EXPORT_BEST_ONLY" not in options
    job = stage.run(tag, ["measure", entry["source"], entry["prepared"], grammar, 1024,
                         "search_gpu_overlap", output], options, timeout=1800, reserve=14400)
    with serial.GPU_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        job.update(task=task, seed=seed, variant=variant, population=1024, generations=64, cases=cases,
                   output=str(output), export=str(export), history=str(history))
        if job["status"] == "ok":
            try:
                doc = serial.read(output)
                development_v2.validate(doc, cases)
                assert len(serial.read(export)["programs"]) == 1024
                job.update(search_s=doc["evolve_wall_ms"] / 1000, exported_program_count=1024)
                for field, path in [("output", output), ("export", export), ("history", history)]:
                    job[field + "_sha256"] = serial.sha(path)
            except Exception as error:
                job.update(status="invalid_output", error=repr(error))
        stage.persist()
    if job["status"] != "ok":
        raise RuntimeError("Old-training confirmation failed; preserve sample, no retry")
    return job


def main(name, prior_name):
    protocol = serial.read(serial.BASE / "protocol.json")
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists()
    state = dict(pid=os.getpid(), status="waiting_for_full30_recipe", started_epoch=time.time(),
                 deadline_epoch=serial.DEADLINE, prior=prior_name,
                 scope="old TRAINING seeds0,1,2; seed0 retains prior full30 samples; no fresh heldout",
                 harness_sha256={str(p): serial.sha(p) for p in [Path(__file__), serial.BASE / "serial.py",
                     serial.BASE / "development_v2.py", serial.BASE / "champion_audit.py",
                     serial.BASE / "settings_followup_v2.py", serial.BASE / "protocol.json"]})
    serial.save(record, state)
    try:
        while True:
            path = serial.RAW / (prior_name + "-controller.json")
            prior = serial.read(path) if path.exists() else {}
            if prior.get("status") == "stopped":
                raise RuntimeError("Prior candidate failed; no confirmation on an unadmitted implementation")
            if prior.get("status") == "requested_phase_complete" and not Path(f"/proc/{prior['pid']}").exists():
                break
            if time.time() >= protocol["development_cutoff_epoch"]:
                raise RuntimeError("Prior recipe missed development cutoff")
            time.sleep(10)
        for path, digest in state["harness_sha256"].items():
            assert serial.sha(path) == digest
        assert prior["task_count"] == 30 and serial.sha(prior["binary"]) == prior["binary_sha256"]
        state.update(status="confirming_old_seeds", binary=prior["binary"], binary_sha256=prior["binary_sha256"],
                     candidate_extra=prior["candidate_extra"], source_build=serial.read(serial.RAW / (prior_name + "-candidate/manifest.json"))["source_build"])
        serial.save(record, state)
        old = serial.read(serial.ROOT / "logs/day24-20261017/manifest.json")
        freeze = dict(reserve_s=14400, variants={"baseline": dict(extra={}), "candidate": dict(extra=prior["candidate_extra"])})
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            stages = {v: serial.Stage(name + "-" + v, protocol["baseline_binary"] if v == "baseline" else prior["binary"],
                                      settings=protocol["baseline_settings"]) for v in freeze["variants"]}
            audits = {v: serial.Stage(name + "-" + v + "-native", protocol["baseline_binary"] if v == "baseline" else prior["binary"],
                                      settings=protocol["baseline_settings"]) for v in freeze["variants"]}
            prior_rows = serial.read(serial.RAW / (prior_name + "-comparisons.json"))
            assert len(prior_rows) == 30 and {r["task"] for r in prior_rows} == set(protocol["tasks"])
            comparisons = [dict(r, seed=0, source_attempt=prior_name) for r in prior_rows]
        for seed in (1, 2):
            for index, task in enumerate(protocol["tasks"]):
                if time.time() >= protocol["development_cutoff_epoch"]:
                    raise RuntimeError("Old-training confirmation cutoff")
                inp = old["inputs"][task]
                entry = inp["quality"][str(seed)]
                with serial.GPU_LOCK.open("a") as lock:
                    fcntl.flock(lock, fcntl.LOCK_EX)
                    for path, digest in [(entry["source"], entry["source_sha256"]),
                                         (entry["prepared"], entry["prepared_sha256"]),
                                         (inp["grammar"], inp["grammar_sha256"])]:
                        assert serial.sha(path) == digest
                count = inp["split"]["train_count"]
                inputs = dict(metadata=dict(train_count=count), grammar=inp["grammar"], seeds={str(seed): entry})
                for variant in (("baseline", "candidate") if (seed + index) % 2 else ("candidate", "baseline")):
                    job = search(stages[variant], task, seed, variant, entry, inp["grammar"], count, freeze["variants"][variant]["extra"])
                    audited = champion_audit.run(audits[variant], job, "train", inputs, freeze)
                    if audited["status"] != "ok":
                        raise RuntimeError("Native training score fidelity failed; no candidate admission")
                with serial.GPU_LOCK.open("a") as lock:
                    fcntl.flock(lock, fcntl.LOCK_EX)
                    rows = settings_followup_v2.compare({v: stages[v].m["jobs"][f"{task}.s{seed}.g64"] for v in stages})
                    for row in rows:
                        row.update(seed=seed, source_attempt=name)
                    comparisons.extend(rows)
                    serial.save(serial.RAW / (name + "-comparisons.json"), comparisons)
                    assert all(r[k] for r in rows for k in ("final_descriptors_equal", "full_final_ast_equal", "champions_equal")), "Old-training trajectory mismatch"
        means = {str(seed): math.exp(sum(math.log(r["speedup"]) for r in comparisons if r["seed"] == seed) / 30) for seed in (0, 1, 2)}
        mean = math.exp(sum(math.log(r["speedup"]) for r in comparisons) / 90)
        for stage in [*stages.values(), *audits.values()]:
            stage.m.update(status="requested_phase_complete", finished_epoch=time.time())
            stage.persist()
        state.update(status="requested_phase_complete", finished_epoch=time.time(), comparisons=90,
                     per_seed_geometric_means=means, geometric_mean=mean, target_observed_in_development=mean >= 1.3,
                     caveat="Development selection; fresh 3-repeat controls and new heldout quality still required")
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--prior-name", required=True)
    args = parser.parse_args()
    main(args.name, args.prior_name)
