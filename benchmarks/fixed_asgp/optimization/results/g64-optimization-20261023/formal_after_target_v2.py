"""Automatically freeze only a fully confirmed candidate meeting the real1.3x target."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import subprocess
import time
import traceback

import capacity_evidence
import freeze_and_run
import serial


def target_ready(development, confirmation):
    freeze_and_run.confirmed_candidate(development, confirmation)
    return confirmation["geometric_mean"] >= 1.3


def main(name, development_name, capacity_name, checks_name, formal_name):
    record = serial.RAW / (name + "-controller.json")
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    assert not record.exists()
    state = dict(pid=os.getpid(), status="waiting_for_confirmed_target", started_epoch=time.time(),
                 deadline_epoch=serial.DEADLINE, development=development_name, capacity=capacity_name,
                 checks=checks_name, formal=formal_name, target=1.3, new_data_started=False)
    serial.save(record, state)
    try:
        while True:
            if (serial.RAW / (formal_name + "-controller.json")).exists():
                state.update(status="external_formal_already_started", finished_epoch=time.time())
                return
            path = serial.RAW / (development_name + "-controller.json")
            dev = serial.read(path) if path.exists() else {}
            if dev.get("status") in ("stopped", "budget_not_run"):
                state.update(status="not_run_prerequisite_failure", finished_epoch=time.time())
                return
            if dev.get("status") == "requested_phase_complete" and not dev.get("performance_admitted"):
                state.update(status="not_run_candidate_rejected", finished_epoch=time.time())
                return
            if dev.get("next_confirmation"):
                path = serial.RAW / (dev["next_confirmation"] + "-controller.json")
                conf = serial.read(path) if path.exists() else {}
                if conf.get("status") == "stopped":
                    raise RuntimeError("Native/trajectory confirmation failed")
                if conf.get("status") == "requested_phase_complete" and not Path(f"/proc/{conf['pid']}").exists():
                    if not target_ready(dev, conf):
                        state.update(status="below_target_needs_development_decision", finished_epoch=time.time(),
                                     confirmed_geometric_mean=conf["geometric_mean"], new_data_started=False,
                                     reason="Do not silently turn the1.3x objective into the2% admission gate")
                        return
                    path = serial.RAW / (capacity_name + "-controller.json")
                    capacity = serial.read(path) if path.exists() else {}
                    if capacity.get("status") in ("requested_phase_complete", "budget_not_run") and not Path(f"/proc/{capacity.get('pid',0)}").exists():
                        path = serial.RAW / checks_name / "manifest.json"
                        checks = serial.read(path) if path.exists() else {}
                        if checks.get("status") == "failed":
                            raise RuntimeError("Final runtime guards failed")
                        if checks.get("status") == "ok" and not Path(f"/proc/{checks['pid']}").exists():
                            break
                    elif capacity.get("status") in ("stopped", "capacity_stopped_preserved", "not_run_prerequisite_failure"):
                        raise RuntimeError("Capacity prerequisite stopped; inspect preserved attempts first")
            assert time.time() < serial.DEADLINE-14400
            time.sleep(10)
        # The original dispatcher only checked controller completion. Read the
        # actual closed parity grid before allowing any fresh-data dispatch.
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            sample = capacity["sample_name"]
            pilot_path = serial.RAW / (sample + "-controller.json")
            pilot = serial.read(pilot_path) if pilot_path.exists() else None
            manifests = ({v: serial.read(serial.RAW / (sample + "-" + v) / "manifest.json")
                          for v in ("baseline", "candidate")} if pilot else {})
            protocol = serial.read(serial.BASE / "protocol.json")
            state["capacity_evidence"] = capacity_evidence.check_capacity(
                capacity, pilot, manifests, dev["binary_sha256"], protocol["baseline_binary_sha256"])
        assert not list(serial.RAW.glob("*-quality-freeze.json"))
        assert not Path(f"/proc/{dev['pid']}").exists()
        log_path = serial.RAW / (formal_name + ".log")
        with log_path.open("x") as log:
            command = ["python3", "-u", str(serial.BASE / "freeze_and_run.py"), "--name", formal_name,
                       "--development", development_name, "--confirmation", dev["next_confirmation"], "--checks", checks_name]
            child = subprocess.Popen(command, cwd=serial.ROOT, env=dict(os.environ, PYTHONPATH=str(serial.ROOT/"tools")),
                                     stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        state.update(status="formal_dispatched", child_pid=child.pid, command=command, finished_epoch=time.time(),
                     caveat="Driver still must recompute source/scale and freeze before creating data; this is not a result claim")
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--development", required=True)
    parser.add_argument("--capacity", required=True)
    parser.add_argument("--checks", required=True)
    parser.add_argument("--formal", required=True)
    args = parser.parse_args()
    main(args.name, args.development, args.capacity, args.checks, args.formal)
