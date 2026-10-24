"""Measure TRAINING-only champion judge costs; never create fresh data."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import time
import traceback

import champion_audit
import common_audit
import serial


def main(name):
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists()
    protocol = serial.read(serial.BASE / "protocol.json")
    state = dict(pid=os.getpid(), status="waiting_for_lock", started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 scope="Old seed0 training only: GAGP common/native plus ASGP common champion costs",
                 expected_tasks=protocol["tasks"], observations=[],
                 harness_sha256={str(p): serial.sha(p) for p in
                                 [Path(__file__), serial.BASE / "serial.py", serial.BASE / "common_audit.py",
                                  serial.BASE / "champion_audit.py", serial.BASE / "protocol.json"]})
    serial.save(record, state)
    try:
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert time.time() < protocol["development_cutoff_epoch"]
            source = serial.read(serial.RAW / "settings2-full-baseline/manifest.json")
            asgp = serial.read(serial.RAW / "asgp-calibration0-search/manifest.json")
            old = serial.read(serial.ROOT / "logs/day24-20261017/manifest.json")
            assert source["status"] == asgp["status"] == "requested_phase_complete"
            stage = serial.Stage(name, protocol["baseline_binary"], settings=protocol["baseline_settings"])
            stage.m.update(harness_sha256=state["harness_sha256"])
            stage.persist()
        cutoff = min(time.time() + 1800, protocol["development_cutoff_epoch"])
        freeze = dict(reserve_s=max(14400, serial.DEADLINE - cutoff),
                      variants={v: dict(extra={}) for v in ("baseline", "asgp")})
        state.update(status="judging_old_training", calibration_cutoff_epoch=cutoff)
        serial.save(record, state)
        for task in protocol["tasks"]:
            spec = protocol["development_inputs"][task]
            inputs = dict(metadata=dict(train_count=spec["train_count"]), grammar=spec["grammar"],
                          seeds={"0": dict(source=spec["source"], prepared=spec["prepared"])})
            baseline_job = dict(source["jobs"][f"{task}.s0.g64"], repetition=0)
            asgp_job = asgp["jobs"][f"{task}.s0.r0.asgp"]
            costs = {}
            for kind, runner, job in (("gagp_common", common_audit.run, baseline_job),
                                      ("gagp_native", champion_audit.run, baseline_job),
                                      ("asgp_common", common_audit.run, asgp_job)):
                result = runner(stage, job, "train", inputs, freeze)
                costs[kind] = {key: result.get(key) for key in ("status", "process_s", "native_fidelity_pass")}
            state["observations"].append(dict(task=task, train_count=spec["train_count"],
                                               forecast_test_count=old["inputs"][task]["split"]["test_count"], costs=costs))
            serial.save(record, state)
        stage.m.update(status="requested_phase_complete", finished_epoch=time.time())
        stage.persist()
        complete = len(state["observations"]) == 30 and all(
            c["status"] == "ok" and (kind != "gagp_native" or c["native_fidelity_pass"] is True)
            for row in state["observations"] for kind, c in row["costs"].items())
        state.update(status="requested_phase_complete", finished_epoch=time.time(), full_cost_observed=complete,
                     caveat="No heldout labels used; test-count metadata only for forecast. Censored jobs are unknown cost.")
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    main(parser.parse_args().name)
