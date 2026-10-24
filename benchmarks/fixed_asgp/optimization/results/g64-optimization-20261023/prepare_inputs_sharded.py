"""Same frozen all30 input cohort, with bounded per-task preparation manifests."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import time
import traceback

import prepare_inputs
import quality_data
import serial


def main(freeze_path, name):
    freeze = serial.read(freeze_path)
    prepare_inputs.validate_freeze(freeze)
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists()
    aggregate = serial.RAW / (name + "-inputs.json")
    assert not aggregate.exists()
    state = dict(pid=os.getpid(), status="preparing", started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 freeze=str(freeze_path), freeze_sha256=serial.sha(freeze_path), inputs={}, task_manifests={},
                 tasks=freeze["tasks"], seeds=freeze["seeds"], input_manifest=str(aggregate),
                 scope="All frozen tasks/seeds unchanged; one ordinary Stage per task bounds controller serialization")
    serial.save(record, state)
    try:
        for task in freeze["tasks"]:
            state["current_task"] = task
            serial.save(record, state)
            if time.time() >= serial.DEADLINE - freeze["reserve_s"]:
                state["inputs"][task] = dict(status="budget_not_run", task=task,
                    seeds={str(seed): dict(status="budget_not_run", inputs_not_read=True) for seed in freeze["seeds"]})
                continue
            with serial.GPU_LOCK.open("a") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                stage = serial.Stage(name + "-" + task, freeze["variants"]["candidate"]["binary"], settings=freeze["settings"])
                stage.m.update(freeze=str(freeze_path), freeze_sha256=state["freeze_sha256"],
                               global_tasks=freeze["tasks"], scheduled_tasks=[task], scheduled_seeds=freeze["seeds"])
                stage.persist()
            # Partition scheduling only. Dataset, seed list, population, limits,
            # variant binaries and the original global freeze remain identical.
            task_freeze = dict(freeze, tasks=[task])
            inputs = quality_data.prepare(stage, task_freeze)
            stage.m.update(status="requested_phase_complete", finished_epoch=time.time())
            stage.persist()
            state["inputs"][task] = inputs[task]
            path = stage.out / "manifest.json"
            state["task_manifests"][task] = dict(path=str(path), sha256=serial.sha(path))
            serial.save(record, state)
        state.update(status="requested_phase_complete", finished_epoch=time.time(),
                     planned_seed_tasks=len(freeze["tasks"]) * len(freeze["seeds"]),
                     admitted_seed_tasks=sum(s.get("status") == "ok" for task in state["inputs"].values() for s in task.get("seeds", {}).values()))
        assert set(state["inputs"]) == set(freeze["tasks"])
        serial.save(aggregate, state)
        state["input_manifest_sha256"] = serial.sha(aggregate)
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--freeze", type=Path, required=True)
    parser.add_argument("--name", required=True)
    args = parser.parse_args()
    main(args.freeze.resolve(), args.name)
