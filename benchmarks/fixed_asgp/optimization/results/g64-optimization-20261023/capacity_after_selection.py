"""Run the declared P16384 old-TRAINING pilot only after confirmed selection."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import subprocess
import time
import traceback

import runtime_forecast_v2
import serial


def budget_after_pilot(now, costs):
    return runtime_forecast_v2.choose_scale(now + 1200, costs)


def main(name, development_name, sample_name):
    record = serial.RAW / (name + "-controller.json")
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    assert not record.exists()
    state = dict(pid=os.getpid(), status="waiting_for_confirmed_candidate", started_epoch=time.time(),
                 deadline_epoch=serial.DEADLINE, development=development_name, sample_name=sample_name,
                 new_heldout=False, forecast_pilot_allowance_s=1200,
                 harness_sha256={str(p): serial.sha(p) for p in [Path(__file__), serial.BASE / "capacity_pilot.py",
                                 serial.BASE / "runtime_forecast_v2.py", serial.BASE / "runtime_forecast.py", serial.BASE / "serial.py"]})
    serial.save(record, state)
    try:
        while True:
            path = serial.RAW / (development_name + "-controller.json")
            prior = serial.read(path) if path.exists() else {}
            if prior.get("status") in ("stopped", "budget_not_run"):
                state.update(status="not_run_prerequisite_failure", finished_epoch=time.time(), no_samples=True)
                return
            if prior.get("status") == "requested_phase_complete":
                if not prior.get("performance_admitted"):
                    state.update(status="not_run_candidate_rejected", finished_epoch=time.time(), no_samples=True)
                    return
                confirmation_name = prior["next_confirmation"]
                path = serial.RAW / (confirmation_name + "-controller.json")
                confirmation = serial.read(path) if path.exists() else {}
                if confirmation.get("status") == "stopped":
                    raise RuntimeError("Native/trajectory confirmation stopped; no capacity samples")
                if confirmation.get("status") == "requested_phase_complete" and not Path(f"/proc/{confirmation['pid']}").exists():
                    break
            assert time.time() < serial.DEADLINE - 14400
            time.sleep(10)
        assert not list(serial.RAW.glob("*-quality-freeze.json"))
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            for path, digest in state["harness_sha256"].items():
                assert serial.sha(path) == digest
            costs, assumptions = runtime_forecast_v2.measured_costs(development_name, confirmation_name)
            forecast = budget_after_pilot(time.time(), costs)
            forecast.update(diagnostic_only=True, assumptions=assumptions,
                            caveat="20min pilot allowance is an estimate; recompute final scale at actual freeze, original deadline unchanged")
            serial.save(serial.RAW / (name + "-forecast.json"), forecast)
            if forecast["selected_seed_count"] is None:
                state.update(status="budget_not_run", finished_epoch=time.time(), no_samples=True,
                             reason="Protect minimum full formal grid; projected20min capacity pilot no longer fits")
                return
        assert not (serial.RAW / (sample_name + "-controller.json")).exists()
        with (serial.RAW / (sample_name + ".log")).open("x") as log:
            child = subprocess.Popen(["python3", "-u", str(serial.BASE / "capacity_pilot.py"), "--name", sample_name,
                                      "--wait-controller", confirmation_name], cwd=serial.ROOT,
                                     env=dict(os.environ, PYTHONPATH=str(serial.ROOT / "tools")),
                                     stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            state.update(status="capacity_running", child_pid=child.pid, confirmation=confirmation_name)
            serial.save(record, state)
            result = child.wait()
        state.update(status="requested_phase_complete" if result == 0 else "capacity_stopped_preserved",
                     returncode=result, finished_epoch=time.time(),
                     limitation="Three tasks and CPU16 only; all30 formal secondary and full native audits remain separate")
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--development", required=True)
    parser.add_argument("--sample-name", required=True)
    args = parser.parse_args()
    main(args.name, args.development, args.sample_name)
