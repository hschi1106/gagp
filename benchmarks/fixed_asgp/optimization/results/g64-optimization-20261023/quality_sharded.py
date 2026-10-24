"""Unchanged all30 quality cohort with independent per-seed stage manifests."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import time
import traceback

import champion_audit
import prepare_inputs
import quality
import serial


def expected_searches(name, tasks, seeds):
    return [dict(task=task, seed=seed, variant=variant, stage=name + f".s{seed}-" + variant,
                 tag=f"{task}.s{seed}.{variant}")
            for seed in seeds for task in tasks for variant in ("baseline", "candidate")]


def main(freeze_path, input_manifest, name):
    freeze = serial.read(freeze_path)
    prepare_inputs.validate_freeze(freeze)
    prepared = serial.read(input_manifest)
    assert prepared["status"] == "requested_phase_complete" and prepared["freeze_sha256"] == serial.sha(freeze_path)
    inputs = prepared["inputs"]
    assert set(inputs) == set(freeze["tasks"])
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists()
    state = dict(pid=os.getpid(), status="search_and_judge", started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 freeze=str(freeze_path), freeze_sha256=serial.sha(freeze_path),
                 input_manifest=str(input_manifest), input_manifest_sha256=serial.sha(input_manifest),
                 expected_searches=expected_searches(name, freeze["tasks"], freeze["seeds"]),
                 seed_manifests={}, not_run_samples=[],
                 scope="Every frozen task/seed/variant retained; per-seed manifests keep JSON logging bounded")
    serial.save(record, state)
    comparisons = []
    try:
        for seed in freeze["seeds"]:
            state["current_seed"] = seed
            if time.time() >= serial.DEADLINE - freeze["reserve_s"]:
                for row in state["expected_searches"]:
                    if row["seed"] >= seed:
                        state["not_run_samples"].append(dict(row, status="budget_not_run", inputs_not_read=True))
                break
            with serial.GPU_LOCK.open("a") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                stages = {v: serial.Stage(name + f".s{seed}-" + v, spec["binary"], settings=freeze["settings"])
                          for v, spec in freeze["variants"].items()}
                judges = {v: serial.Stage(name + f".s{seed}-" + v + "-judges", spec["binary"], settings=freeze["settings"])
                          for v, spec in freeze["variants"].items()}
                state["seed_manifests"][str(seed)] = {}
                for variant in stages:
                    for kind, stage in [("search", stages[variant]), ("judges", judges[variant])]:
                        stage.m.update(freeze=str(freeze_path), freeze_sha256=state["freeze_sha256"], scheduled_seed=seed,
                                       global_tasks=freeze["tasks"], global_seeds=freeze["seeds"])
                        stage.persist()
                        state["seed_manifests"][str(seed)][variant + "_" + kind] = str(stage.out / "manifest.json")
            serial.save(record, state)
            for index, task in enumerate(freeze["tasks"]):
                order = ("baseline", "candidate") if (seed + index) % 2 else ("candidate", "baseline")
                for variant in order:
                    run = quality.search(stages[variant], freeze, task, seed, variant, inputs[task])
                    for split in ("train", "test"):
                        quality.judge(judges[variant], run, split, inputs[task], freeze["reserve_s"])
                        champion_audit.run(judges[variant], run, split, inputs[task], freeze)
                with serial.GPU_LOCK.open("a") as lock:
                    fcntl.flock(lock, fcntl.LOCK_EX)
                    a, b = [stages[v].m["jobs"][f"{task}.s{seed}.{v}"] for v in ("baseline", "candidate")]
                    pair = dict(task=task, seed=seed, statuses=[a["status"], b["status"]],
                                common_audit_statuses=[judges[v].m["jobs"][f"{task}.s{seed}.{v}.{split}"]["status"]
                                                       for v in ("baseline", "candidate") for split in ("train", "test")],
                                native_audit_statuses=[judges[v].m["jobs"][f"{task}.s{seed}.{v}.{split}.native"]["status"]
                                                       for v in ("baseline", "candidate") for split in ("train", "test")])
                    if a["status"] == b["status"] == "ok":
                        x, y = [serial.read(job["output"]) for job in (a, b)]
                        histories = [[(r["ast"], r["fitness"]) for r in serial.read(job["history"])["history"]] for job in (a, b)]
                        pair.update(final_descriptors_equal=x["final_population"] == y["final_population"],
                                    champion_ast_fitness_equal=histories[0] == histories[1],
                                    best_export_equal=a["export_sha256"] == b["export_sha256"])
                    comparisons.append(pair)
                    serial.save(serial.RAW / (name + "-comparisons.json"), comparisons)
            for stage in [*stages.values(), *judges.values()]:
                stage.m.update(status="requested_phase_complete", finished_epoch=time.time())
                stage.persist()
            state["completed_seed"] = seed
            serial.save(record, state)
        state.update(status="requested_phase_complete", finished_epoch=time.time(), recorded_pairs=len(comparisons),
                     caveat="Work completion only; all expected rows, native audits and source/input hashes qualify actual results")
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
