"""Measure old-training full-population native audit cost without new searches."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import time
import traceback

import full_population_audit
import serial


def main(name, wait_controller):
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists()
    protocol = serial.read(serial.BASE / "protocol.json")
    state = dict(pid=os.getpid(), status="waiting_for_profile", started_epoch=time.time(),
                 deadline_epoch=serial.DEADLINE, waiting_for=wait_controller,
                 scope="Old TRAINING/full30/P1024/G64 final ASTs, cost only; no new searches or heldout",
                 expected_tasks=protocol["tasks"], audit_results=[],
                 harness_sha256={str(p): serial.sha(p) for p in
                                 [Path(__file__), serial.BASE / "full_population_audit.py",
                                  serial.BASE / "champion_audit.py", serial.BASE / "serial.py",
                                  serial.BASE / "protocol.json"]})
    serial.save(record, state)
    try:
        while True:
            path = serial.RAW / (wait_controller + "-controller.json")
            previous = serial.read(path) if path.exists() else {}
            if previous.get("status") in ("requested_phase_complete", "stopped") and not Path(f"/proc/{previous['pid']}").exists():
                break
            assert time.time() < protocol["development_cutoff_epoch"], "No clock reset"
            time.sleep(10)
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            for path, digest in state["harness_sha256"].items():
                assert serial.sha(path) == digest
            source = serial.read(serial.RAW / "settings2-full-baseline/manifest.json")
            assert source["status"] == "requested_phase_complete"
            assert source["binary_sha256"] == protocol["baseline_binary_sha256"]
            stage = serial.Stage(name, protocol["baseline_binary"], settings=protocol["baseline_settings"])
            stage.m.update(harness_sha256=state["harness_sha256"], source_manifest=str(serial.RAW / "settings2-full-baseline/manifest.json"))
            stage.persist()
        # A declared cost-calibration budget. Censoring stays unknown rather
        # than becoming an optimistic zero-cost estimate or a hidden retry.
        cutoff = min(time.time() + 1800, protocol["development_cutoff_epoch"])
        state.update(status="measuring_native_cost", calibration_cutoff_epoch=cutoff)
        serial.save(record, state)
        freeze = dict(reserve_s=max(14400, serial.DEADLINE - cutoff),
                      full_audit_timeout_s=600, variants={"baseline": dict(extra={})})
        for task in protocol["tasks"]:
            old_job = source["jobs"][f"{task}.s0.g64"]
            assert old_job["exported_program_count"] == 1024 and old_job["status"] == "ok"
            entry = protocol["development_inputs"][task]
            inputs = dict(source=entry["source"], grammar=entry["grammar"],
                          prefixes={"1024": dict(prepared=entry["prepared"])})
            start = time.time()
            job = full_population_audit.run(stage, dict(old_job, repetition=0), inputs, freeze)
            state["audit_results"].append(dict(task=task, status=job["status"], elapsed_s=time.time() - start,
                                                process_s=job.get("process_s"),
                                                native_fidelity_pass=job.get("native_fidelity_pass"),
                                                unique_programs=job.get("native_cpu_executed_programs")))
            serial.save(record, state)
        stage.m.update(status="requested_phase_complete", finished_epoch=time.time())
        stage.persist()
        qualified = [r for r in state["audit_results"] if r["status"] == "ok" and r["native_fidelity_pass"]]
        state.update(status="requested_phase_complete", finished_epoch=time.time(),
                     qualified_tasks=len(qualified), full_cost_observed=len(qualified) == 30,
                     elapsed_s=sum(r["elapsed_s"] for r in state["audit_results"]),
                     caveat="Cost only. Failed/timed/budget rows are censored, not zero cost or passed fidelity.")
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
