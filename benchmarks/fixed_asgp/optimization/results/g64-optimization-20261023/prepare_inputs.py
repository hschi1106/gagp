"""Prepare one shared fresh input cohort only after the immutable quality freeze."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import time
import traceback

import quality_data
import serial


def validate_freeze(freeze):
    protocol = serial.read(serial.BASE / "protocol.json")
    assert freeze["format"] == "gagp-g64-quality-freeze-v1"
    assert freeze["started_epoch"] == serial.START and freeze["deadline_epoch"] == serial.DEADLINE
    assert freeze["population"] == 1024 and freeze["generations"] == 64
    assert freeze["tasks"] == protocol["tasks"] and set(freeze["variants"]) == {"baseline", "candidate"}
    assert len(freeze["seeds"]) >= 10 and min(freeze["seeds"]) >= 35
    assert len(set(freeze["seeds"])) == len(freeze["seeds"])
    assert freeze["seeds"] == list(range(35, 35 + len(freeze["seeds"])))
    assert freeze["data_seed"] == 20261024 and freeze["reserve_s"] >= 14400
    assert freeze["train_limit"] == 200 and freeze["test_limit"] == 1000
    for spec in freeze["variants"].values():
        settings = dict(freeze["settings"], **spec["extra"])
        assert settings["GAGP_HOST_THREADS"] == "1"
        assert settings["GAGP_GPU_PAYLOAD_WORKSPACE_MIB"] == "16384"
        assert settings["GAGP_GPU_PHASE_POPULATION"] == "1"
    assert freeze["first_heldout_use"] == "after immutable candidate/scale freeze"
    for path, digest in freeze["harness_sha256"].items():
        assert serial.sha(path) == digest, "Frozen preparation harness changed"
    for spec in freeze["variants"].values():
        assert serial.sha(spec["binary"]) == spec["binary_sha256"]


def main(freeze_path, name):
    freeze = serial.read(freeze_path)
    validate_freeze(freeze)
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists(), "Existing preparation must be preserved and explicitly recovered"
    state = dict(pid=os.getpid(), status="initializing", started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 freeze=str(freeze_path), freeze_sha256=serial.sha(freeze_path))
    serial.save(record, state)
    try:
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            stage = serial.Stage(name, freeze["variants"]["candidate"]["binary"], settings=freeze["settings"])
            stage.m.update(freeze=str(freeze_path), freeze_sha256=state["freeze_sha256"])
            stage.persist()
        state["status"] = "preparing"
        serial.save(record, state)
        inputs = quality_data.prepare(stage, freeze)
        stage.m.update(status="requested_phase_complete", finished_epoch=time.time())
        stage.persist()
        state.update(status="requested_phase_complete", finished_epoch=time.time(), input_manifest=str(stage.out / "manifest.json"),
                     prepared_tasks=sum(x.get("status") == "prepared" for x in inputs.values()),
                     admitted_seed_tasks=sum(x.get("status") == "ok" for task in inputs.values() for x in task.get("seeds", {}).values()),
                     planned_seed_tasks=len(freeze["tasks"]) * len(freeze["seeds"]),
                     caveat="Requested preparation coverage; failures remain explicit and no populations were resampled")
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
