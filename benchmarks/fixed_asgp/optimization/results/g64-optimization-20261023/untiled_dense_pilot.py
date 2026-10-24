"""Measure the admitted dense kernel on its actual untiled layout, preserving prior negative results."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import time
import traceback

import dense_pilot
import development_v2
import serial


def main(name):
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists()
    protocol = serial.read(serial.BASE / "protocol.json")
    prior = serial.read(serial.RAW / "dense3-controller.json")
    build = serial.read(serial.RAW / "build-attempt3/manifest.json")
    assert prior["status"] == "requested_phase_complete" and build["status"] == "ok"
    assert all(not Path(f"/proc/{r['pid']}").exists() for r in (prior, build))
    extra = dict(prior["existing_extra"], GAGP_GPU_TILED_INITIAL_PAYLOAD="0")
    state = dict(pid=os.getpid(), status="coarse_g64", started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 binary=build["binary"], binary_sha256=build["binary_sha256"], source_build="build-attempt3",
                 candidate_extra=dict(extra, GAGP_GPU_DENSE_CASE_LANES="1"),
                 scope="New global untiled layout, original auto-tiled baseline and fresh ordinary-untiled control; old TRAINING only",
                 previous_negative_attempt="dense3, only Grade activated under the inherited tile policy",
                 harness_sha256={str(p): serial.sha(p) for p in
                                 [Path(__file__), serial.BASE / "dense_pilot.py", serial.BASE / "serial.py",
                                  serial.BASE / "development_v2.py", serial.BASE / "settings_followup_v2.py", serial.BASE / "protocol.json"]})
    serial.save(record, state)
    try:
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert all(serial.sha(serial.ROOT / p) == digest for p, digest in build["source_sha256"].items())
            assert serial.sha(build["binary"]) == build["binary_sha256"]
        build["name"] = "build-attempt3"
        means, active = dense_pilot.matrix(name + "-coarse", development_v2.SCREEN_TASKS, protocol, build, extra, state["harness_sha256"])
        admitted = active > 0 and means["candidate"] >= 1.02 and means["candidate"] / means["ordinary"] >= 1.02
        state.update(coarse_geometric_means=means, coarse_active_generations=active, coarse_performance_admitted=admitted)
        serial.save(record, state)
        if admitted:
            state["status"] = "full30_g64"
            serial.save(record, state)
            full, active = dense_pilot.matrix(name, protocol["tasks"], protocol, build, extra, state["harness_sha256"])
            state.update(task_count=30, geometric_mean=full["candidate"], geometric_means=full,
                         candidate_over_ordinary=full["candidate"] / full["ordinary"], active_generations=active,
                         performance_admitted=full["candidate"] >= 1.02 and full["candidate"] / full["ordinary"] >= 1.02,
                         target_observed_in_development=full["candidate"] >= 1.3)
        else:
            state.update(task_count=0, performance_admitted=False)
        state.update(status="requested_phase_complete", finished_epoch=time.time(),
                     caveat="This explicit layout attempt retains every prior auto-tiled negative row; not fresh validation")
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    main(parser.parse_args().name)
