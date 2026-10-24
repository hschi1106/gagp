"""Measure large judge calls using repeated TRAINING cases, never heldout labels."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import time
import traceback

import asgp_audit
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
                 diagnostic_only=True, new_heldout=False, observations=[],
                 scope="At least old test-count-sized judge calls from repeated OLD TRAINING cases; cost only, never quality outcomes",
                 harness_sha256={str(p): serial.sha(p) for p in
                                 [Path(__file__), serial.BASE / "serial.py", serial.BASE / "common_audit.py",
                                  serial.BASE / "champion_audit.py", serial.BASE / "asgp_audit.py", serial.BASE / "protocol.json"]})
    serial.save(record, state)
    try:
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert time.time() < protocol["development_cutoff_epoch"]
            primary = serial.read(serial.RAW / "settings2-full-baseline/manifest.json")
            asgp = serial.read(serial.RAW / "asgp-calibration0-search/manifest.json")
            old = serial.read(serial.ROOT / "logs/day24-20261017/manifest.json")
            assert primary["status"] == asgp["status"] == "requested_phase_complete"
            stage = serial.Stage(name, protocol["baseline_binary"], settings=protocol["baseline_settings"])
            stage.m.update(diagnostic_only=True, new_heldout=False, harness_sha256=state["harness_sha256"])
            stage.persist()
        cutoff = min(time.time() + 1800, protocol["development_cutoff_epoch"])
        freeze = dict(reserve_s=max(14400, serial.DEADLINE - cutoff), variants={v: dict(extra={}) for v in ("baseline", "asgp")})
        state.update(status="measuring_training_replication", calibration_cutoff_epoch=cutoff)
        serial.save(record, state)
        for task in protocol["tasks"]:
            if time.time() >= cutoff:
                state["observations"].append(dict(task=task, status="budget_not_run", inputs_not_read=True))
                continue
            spec = protocol["development_inputs"][task]
            with serial.GPU_LOCK.open("a") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                if time.time() >= cutoff:
                    state["observations"].append(dict(task=task, status="budget_not_run", inputs_not_read=True))
                    continue
                training = serial.read(spec["source"])
                count = max(spec["train_count"], old["inputs"][task]["split"]["test_count"])
                rows = [training["cases"][i % len(training["cases"])] for i in range(count)]
                source = dict(training, cases=rows, population=[],
                              quality_split="repeated_old_training_cost_probe",
                              diagnostic_scope="Repeated old TRAINING cases; not a test split or quality observation")
                path = stage.out / (task + ".repeated-training.json")
                assert not path.exists()
                serial.save(path, source)
                inputs = dict(metadata=dict(train_count=spec["train_count"], test_count=count),
                              grammar=spec["grammar"], test_source=str(path),
                              seeds={"0": dict(source=spec["source"], prepared=spec["prepared"])})
            baseline_job = dict(primary["jobs"][f"{task}.s0.g64"], repetition=0)
            asgp_job = asgp["jobs"][f"{task}.s0.r0.asgp"]
            costs = {}
            for kind, function, job in (("gagp_common", common_audit.run, baseline_job),
                                         ("gagp_native", champion_audit.run, baseline_job),
                                         ("asgp_common", common_audit.run, asgp_job),
                                         ("asgp_native", asgp_audit.run, asgp_job)):
                result = function(stage, job, "test", inputs, freeze)
                costs[kind] = {k: result.get(k) for k in ("status", "process_s", "native_fidelity_pass")}
            state["observations"].append(dict(task=task, status="measured", original_train_count=spec["train_count"],
                                               probe_count=count, forecast_test_count=old["inputs"][task]["split"]["test_count"],
                                               source=str(path), source_sha256=serial.sha(path), costs=costs))
            serial.save(record, state)
        stage.m.update(status="requested_phase_complete", finished_epoch=time.time())
        stage.persist()
        complete = len(state["observations"]) == 30 and all(
            r["status"] == "measured" and all(c["status"] == "ok" and
                (not kind.endswith("native") or c["native_fidelity_pass"] is True) for kind, c in r["costs"].items())
            for r in state["observations"])
        state.update(status="requested_phase_complete", finished_epoch=time.time(), full_cost_observed=complete,
                     limitation="Repeated inputs can benefit GPU input reuse; CPU case loops and JSON sizes are measured. Forecast still needs safety for new inputs/genotypes; not heldout evidence.")
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    main(parser.parse_args().name)
