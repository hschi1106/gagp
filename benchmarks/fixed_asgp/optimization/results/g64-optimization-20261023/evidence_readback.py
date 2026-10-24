"""Exercise final readers on preserved full30 OLD TRAINING audit artifacts."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import time
import traceback

import report_evidence
import serial


def main(name, checks_name):
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists()
    state = dict(pid=os.getpid(), status="waiting_for_guards", started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 new_data=False, scope="Analysis reader validation only; existing OLD TRAINING artifacts, no new runtime/search/CPU references")
    serial.save(record, state)
    try:
        while True:
            path = serial.RAW / checks_name / "manifest.json"
            checks = serial.read(path) if path.exists() else {}
            if checks.get("status") == "failed":
                raise RuntimeError("Reader guards failed; inspect preserved log")
            if checks.get("status") == "ok" and not Path(f"/proc/{checks['pid']}").exists():
                break
            assert time.time() < serial.DEADLINE - 14400
            time.sleep(10)
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert time.time() < serial.DEADLINE - 14400
            state["status"] = "reading_old_training_evidence"
            serial.save(record, state)
            reader = report_evidence.Evidence()
            p = serial.read(serial.BASE / "protocol.json")
            search = serial.read(serial.RAW / "settings2-full-baseline/manifest.json")["jobs"]
            manifests = {n: list(serial.read(serial.RAW / n / "manifest.json")["jobs"].values())
                         for n in ("full-audit-cost0", "quality-cost0", "judge-cost-large0")}
            for task in p["tasks"]:
                job = search[f"{task}.s0.g64"]
                data = reader.artifact(job)
                export = reader.artifact(job, "export")["programs"]
                observations = [r["ast"] for r in reader.artifact(job, "history")["history"]] + [export[0]]
                full = next(j for j in manifests["full-audit-cost0"] if j["task"] == task)
                reader.native(full, data, "train", data["cases"], export, full=True)
                for split, stage_name in (("train", "quality-cost0"), ("test", "judge-cost-large0")):
                    native = next(j for j in manifests[stage_name] if j["task"] == task and j["variant"] == "baseline" and j.get("diagnostic"))
                    common = next(j for j in manifests[stage_name] if j["task"] == task and j["variant"] == "baseline" and not j.get("diagnostic"))
                    reader.native(native, data, split, native["cases"], observations)
                    reader.common(common, task, common["cases"])
                state["completed_tasks"] = state.get("completed_tasks", 0) + 1
                serial.save(record, state)
            state.update(status="requested_phase_complete", finished_epoch=time.time(), verified_files=len(reader.hashes),
                         native_calls=reader.native_calls, native_observations=reader.native_observations,
                         note="Reader readback passed; native executions were the original recorded calibration calls, not fresh regressions or formal quality")
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--checks-name", required=True)
    args = parser.parse_args()
    main(args.name, args.checks_name)
