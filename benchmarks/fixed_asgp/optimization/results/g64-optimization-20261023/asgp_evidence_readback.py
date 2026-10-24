"""Validate ASGP report glue using this round's existing OLD TRAINING artifacts."""
from argparse import ArgumentParser
import fcntl
import os
import time
import traceback

import report_evidence
import report_speed
import serial


def main(name):
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists()
    state = dict(pid=os.getpid(), status="waiting_for_lock", started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 new_data=False, diagnostic_only=True,
                 scope="Read existing30-task ASGP/common/native train+repeatedTRAINING large-call artifacts; no search/runtime/audit execution")
    serial.save(record, state)
    try:
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert time.time() < serial.DEADLINE - 14400
            state["status"] = "reading_old_training_evidence"
            serial.save(record, state)
            p = serial.read(serial.BASE / "protocol.json")
            searches = serial.read(serial.RAW / "asgp-calibration0-search/manifest.json")["jobs"]
            audits = {}
            for source in ("asgp-calibration0-native", "quality-cost0", "judge-cost-large0"):
                audits.update({key: value for key, value in serial.read(serial.RAW / source / "manifest.json")["jobs"].items()
                               if value.get("variant") == "asgp"})
            large = serial.read(serial.RAW / "judge-cost-large0-controller.json")
            large = {row["task"]: row for row in large["observations"]}
            reader = report_evidence.Evidence()
            for task in p["tasks"]:
                job = searches[f"{task}.s0.r0.asgp"]
                inputs = dict(seeds={"0": dict(source=p["development_inputs"][task]["source"])},
                              test_source=large[task]["source"],
                              metadata=dict(train_count=p["development_inputs"][task]["train_count"], test_count=large[task]["probe_count"]))
                report_speed.audit_r0(reader, audits, job, reader.artifact(job), dict(population=1024), inputs)
                state["completed_tasks"] = state.get("completed_tasks", 0) + 1
                serial.save(record, state)
            state.update(status="requested_phase_complete", finished_epoch=time.time(), verified_files=len(reader.hashes),
                         native_calls=reader.native_calls,
                         note="Analysis readback passed; original native CPU calls were calibration observations, not new formal references")
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    args = parser.parse_args()
    main(args.name)
