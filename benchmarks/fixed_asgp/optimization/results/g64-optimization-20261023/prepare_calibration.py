"""Measure source initialization and native admission on old seeds only."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import time
import traceback

import serial


def main(name, wait_controller):
    protocol = serial.read(serial.BASE / "protocol.json")
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists()
    state = dict(pid=os.getpid(), status="waiting", started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 waiting_for=wait_controller, scope="old seed0 initialization/admission costs only; no new heldout",
                 harness_sha256={str(p): serial.sha(p) for p in [Path(__file__), serial.BASE / "serial.py", serial.BASE / "protocol.json"]})
    serial.save(record, state)
    try:
        while True:
            path = serial.RAW / (wait_controller + "-controller.json")
            previous = serial.read(path) if path.exists() else {}
            if previous.get("status") in ("requested_phase_complete", "stopped") and not Path(f"/proc/{previous['pid']}").exists():
                break
            if time.time() >= protocol["development_cutoff_epoch"]:
                raise RuntimeError("Preparation calibration exceeded development cutoff")
            time.sleep(10)
        for path, digest in state["harness_sha256"].items():
            assert serial.sha(path) == digest
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            stage = serial.Stage(name, protocol["baseline_binary"], settings=protocol["baseline_settings"])
        state["status"] = "measuring_old_seed_preparation"
        serial.save(record, state)
        for task in protocol["tasks"]:
            if time.time() >= protocol["development_cutoff_epoch"]:
                raise RuntimeError("Preparation calibration cutoff")
            source = stage.out / (task + ".s0.source.json")
            job = stage.run(task + ".freeze", ["freeze", task, source],
                            {"GAGP_FREEZE_POPULATION": "1024", "GAGP_FREEZE_POPULATION_SEED": "0"},
                            timeout=600, reserve=14400)
            if job["status"] != "ok":
                raise RuntimeError("Old initialization calibration failed; keep attempt")
            prepared = stage.out / (task + ".s0.prepared.json")
            spec = protocol["development_inputs"][task]
            # Admission consumes the existing old training source; the freshly
            # initialized source above is only a source-generation cost probe.
            job = stage.run(task + ".prepare", ["prepare", spec["source"], spec["grammar"], 1024, prepared],
                            timeout=600, reserve=14400)
            with serial.GPU_LOCK.open("a") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                if job["status"] == "ok":
                    document = serial.read(prepared)
                    if len(document["programs"]) != 1024 or document.get("admission_rejections"):
                        job["status"] = "native_unadmitted"
                    else:
                        job["output_sha256"] = serial.sha(prepared)
                stage.persist()
            if job["status"] != "ok":
                raise RuntimeError("Old admission calibration failed; keep attempt")
        stage.m.update(status="requested_phase_complete", finished_epoch=time.time())
        stage.persist()
        state.update(status="requested_phase_complete", finished_epoch=time.time(), tasks=30,
                     one_seed_cohort_process_s=sum(j["process_s"] for j in stage.m["jobs"].values()),
                     caveat="Cost forecast only; source probe outputs are not new training/test fixtures or formal cohorts")
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--wait-controller", required=True)
    args = parser.parse_args()
    main(args.name, args.wait_controller)
