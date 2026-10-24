"""G64 old-training view launch geometry, with one full30 global recipe."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import time
import traceback

import development_v2
import serial
import settings_followup_v2


ARMS = {
    "baseline": {},
    "view32": {"GAGP_GPU_VIEW_PHYSICAL_BLOCK": "32", "GAGP_GPU_RELAXED_VIEW_REGISTERS": "1"},
    "view64": {"GAGP_GPU_VIEW_PHYSICAL_BLOCK": "64", "GAGP_GPU_RELAXED_VIEW_REGISTERS": "1"},
    "view128": {"GAGP_GPU_VIEW_PHYSICAL_BLOCK": "128", "GAGP_GPU_RELAXED_VIEW_REGISTERS": "1"},
}


def main(name, wait_controller):
    protocol = serial.read(serial.BASE / "protocol.json")
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists()
    state = dict(pid=os.getpid(), status="waiting", started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 waiting_for=wait_controller, arms=ARMS,
                 scope="8 old TRAINING G64 tasks, then one global recipe full30; no new heldout",
                 harness_sha256={str(p): serial.sha(p) for p in [Path(__file__), serial.BASE / "serial.py",
                     serial.BASE / "development_v2.py", serial.BASE / "settings_followup_v2.py", serial.BASE / "protocol.json"]})
    serial.save(record, state)
    try:
        while True:
            path = serial.RAW / (wait_controller + "-controller.json")
            previous = serial.read(path) if path.exists() else {}
            if previous.get("status") == "requested_phase_complete" and not Path(f"/proc/{previous['pid']}").exists():
                break
            if previous.get("status") == "stopped":
                raise RuntimeError("Prior setting screen failed")
            if time.time() >= protocol["development_cutoff_epoch"]:
                raise RuntimeError("Phase setting screen cutoff")
            time.sleep(10)
        for path, digest in state["harness_sha256"].items():
            assert serial.sha(path) == digest
        state["status"] = "coarse_g64"
        serial.save(record, state)
        means = settings_followup_v2.matrix(name + "-coarse", development_v2.SCREEN_TASKS, ARMS, protocol)
        winner = max(means, key=means.get)
        state.update(coarse_geometric_means=means, coarse_winner=winner)
        serial.save(record, state)
        if means[winner] >= 1.02:
            state["status"] = "full30_g64"
            serial.save(record, state)
            full = settings_followup_v2.matrix(name + "-full", protocol["tasks"],
                                              {"baseline": {}, "candidate": ARMS[winner]}, protocol)
            state.update(full30_geometric_means=full, selected_extra=ARMS[winner],
                         binary=protocol["baseline_binary"], binary_sha256=protocol["baseline_binary_sha256"],
                         performance_admitted=full["candidate"] >= 1.02)
        else:
            state.update(selected_extra={}, performance_admitted=False)
        state.update(status="requested_phase_complete", finished_epoch=time.time(),
                     caveat="Development/selection; every final AST, score descriptor and generation champion compared. Not independent quality proof.")
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
