"""Old-training ASGP G64 runtime/native-audit calibration, never formal ratios."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import time
import traceback

import asgp_audit
import serial
import speed


def main(name, wait_controller):
    protocol = serial.read(serial.BASE / "protocol.json")
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists(), "Preserve previous attempts"
    state = dict(pid=os.getpid(), status="waiting", started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 waiting_for=wait_controller, scope="old TRAINING seed0, P1024/G64; cost calibration only",
                 harness_sha256={str(p): serial.sha(p) for p in [Path(__file__), serial.BASE / "serial.py",
                                  serial.BASE / "speed.py", serial.BASE / "development_v2.py",
                                  serial.BASE / "asgp_audit.py", serial.BASE / "champion_audit.py", serial.BASE / "protocol.json"]})
    serial.save(record, state)
    try:
        while True:
            path = serial.RAW / (wait_controller + "-controller.json")
            previous = serial.read(path) if path.exists() else {}
            if previous.get("status") in ("requested_phase_complete", "stopped") and not Path(f"/proc/{previous['pid']}").exists():
                break
            if time.time() >= protocol["development_cutoff_epoch"]:
                raise RuntimeError("Calibration missed development cutoff")
            time.sleep(10)
        for path, digest in state["harness_sha256"].items():
            assert serial.sha(path) == digest, "Calibration harness changed after launch"
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            stage = serial.Stage(name + "-search", protocol["baseline_binary"], settings=protocol["baseline_settings"])
            audits = serial.Stage(name + "-native", protocol["baseline_binary"], settings=protocol["baseline_settings"])
        freeze = dict(seed=0, reserve_s=14400, variants={"asgp": dict(mode="asgp_1t", extra={})})
        state["status"] = "old_training_searches_and_native_audits"
        serial.save(record, state)
        for task in protocol["tasks"]:
            if time.time() >= protocol["development_cutoff_epoch"]:
                raise RuntimeError("Calibration cutoff; keep completed, failed and remaining status")
            spec = protocol["development_inputs"][task]
            inputs = dict(status="prepared", metadata=dict(train_count=spec["train_count"]),
                          grammar=spec["grammar"], seeds={"0": dict(status="ok", source=spec["source"], prepared=spec["prepared"])})
            job = speed.search(stage, freeze, task, 0, "asgp", inputs)
            audit = asgp_audit.run(audits, job, "train", inputs, freeze)
            if job["status"] != "ok" or audit["status"] != "ok":
                raise RuntimeError("ASGP native calibration failed; preserve result, do not retry")
        for one in (stage, audits):
            one.m.update(status="requested_phase_complete", finished_epoch=time.time())
            one.persist()
        state.update(status="requested_phase_complete", finished_epoch=time.time(),
                     tasks=30, search_process_s=sum(j["process_s"] for j in stage.m["jobs"].values()),
                     native_process_s=sum(j["process_s"] for j in audits.m["jobs"].values()),
                     caveat="Not fresh-heldout controls; never substitute these times in final ASGP speedups")
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
