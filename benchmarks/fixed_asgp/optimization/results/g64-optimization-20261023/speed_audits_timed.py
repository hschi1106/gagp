"""Finish explicit R0 native/full-population and train/test speed-cohort audits."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import time
import traceback

import serial
import budgeted_audits


def main(freeze_path, input_manifest, speed_name, name):
    freeze = serial.read(freeze_path)
    assert freeze["format"] == "gagp-g64-speed-freeze-v1"
    assert freeze["started_epoch"] == serial.START and freeze["deadline_epoch"] == serial.DEADLINE
    record = serial.RAW / (name + "-controller.json")
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    assert not record.exists()
    state = dict(pid=os.getpid(), status="waiting_for_speed_searches", started_epoch=time.time(),
                 deadline_epoch=serial.DEADLINE, freeze=str(freeze_path), freeze_sha256=serial.sha(freeze_path),
                 scope="R0 all generation/final champions train/test; both GPU R0 full final populations. R1/R2 have CPU16 and paired champion/best/score-descriptor checks only")
    serial.save(record, state)
    stages = {}
    try:
        while True:
            path = serial.RAW / (speed_name + "-controller.json")
            previous = serial.read(path) if path.exists() else {}
            if previous.get("status") == "stopped":
                raise RuntimeError("Speed controller stopped; inspect and recover preserved records explicitly")
            if previous.get("status") == "requested_phase_complete" and not Path(f"/proc/{previous['pid']}").exists():
                break
            if time.time() >= serial.DEADLINE - freeze["reserve_s"]:
                raise RuntimeError("Audit prerequisites missed timed cutoff")
            time.sleep(10)
        prepared = serial.read(input_manifest)
        assert prepared["freeze_sha256"] == freeze["quality_freeze_sha256"]
        inputs = prepared["inputs"]
        manifests = {v: serial.read(serial.RAW / (speed_name + "-" + v) / "manifest.json")
                     for v in freeze["variants"]}
        for variant, manifest in manifests.items():
            assert manifest["freeze_sha256"] == state["freeze_sha256"]
            assert manifest["binary_sha256"] == freeze["variants"][variant]["binary_sha256"]
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            stages = {v: serial.Stage(name + "-" + v, spec["binary"], settings=freeze["settings"])
                      for v, spec in freeze["variants"].items()}
        state["status"] = "native_and_common_r0_audits"
        serial.save(record, state)
        for task in freeze["tasks"]:
            for variant in freeze["variants"]:
                job = manifests[variant]["jobs"][f"{task}.s{freeze['seed']}.r0.{variant}"]
                if variant != "asgp":
                    entry = inputs[task]["seeds"].get(str(freeze["seed"]), {})
                    full_inputs = dict(source=entry.get("source"), grammar=inputs[task]["grammar"],
                                       prefixes={"1024": dict(prepared=entry.get("prepared"))})
                    budgeted_audits.full(stages[variant], job, full_inputs, freeze)
                for split in ("train", "test"):
                    budgeted_audits.common(stages[variant], job, split, inputs[task], freeze)
                    if variant == "asgp":
                        budgeted_audits.asgp(stages[variant], job, split, inputs[task], freeze)
                    else:
                        budgeted_audits.run(stages[variant], job, split, inputs[task], freeze)
        for stage in stages.values():
            stage.m.update(status="requested_phase_complete", finished_epoch=time.time())
            stage.persist()
        state.update(status="requested_phase_complete", finished_epoch=time.time(),
                     actual_jobs=sum(len(s.m["jobs"]) for s in stages.values()),
                     caveat="Requested audit phase ended. Actual job coverage/fidelity/errors qualify results independently.")
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--freeze", type=Path, required=True)
    parser.add_argument("--input-manifest", type=Path, required=True)
    parser.add_argument("--speed-name", required=True)
    parser.add_argument("--name", required=True)
    args = parser.parse_args()
    main(args.freeze.resolve(), args.input_manifest.resolve(), args.speed_name, args.name)
