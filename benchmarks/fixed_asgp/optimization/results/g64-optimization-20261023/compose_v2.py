"""Measure the selected G64 implementation/settings together on all30 tasks."""
from argparse import ArgumentParser
import fcntl
import math
import os
from pathlib import Path
import time
import traceback

import development_v2 as development
import serial
import settings_followup_v2 as settings_followup


def main(name, setting_name, trim_name, build_name):
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists(), "Preserve prior attempts"
    protocol = serial.read(serial.BASE / "protocol.json")
    state = dict(pid=os.getpid(), status="waiting_for_components", started_epoch=time.time(),
                 deadline_epoch=serial.DEADLINE, waiting_for=[setting_name, trim_name],
                 scope="all30 old TRAINING G64/P1024; candidate selection, not fresh validation",
                 harness_sha256={str(p): serial.sha(p) for p in
                                 [Path(__file__), serial.BASE / "serial.py", serial.BASE / "development_v2.py",
                                  serial.BASE / "settings_followup_v2.py", serial.BASE / "protocol.json"]})
    serial.save(record, state)
    stages = {}
    try:
        while True:
            entries = []
            for component in (setting_name, trim_name):
                path = serial.RAW / (component + "-controller.json")
                entries.append(serial.read(path) if path.exists() else {})
            if any(e.get("status") == "stopped" for e in entries):
                raise RuntimeError("Component stopped; inspect actual failure before composing")
            if all(e.get("status") == "requested_phase_complete" and not Path(f"/proc/{e['pid']}").exists() for e in entries):
                break
            if time.time() >= protocol["development_cutoff_epoch"]:
                raise RuntimeError("Components missed development cutoff")
            time.sleep(10)
        for path, digest in state["harness_sha256"].items():
            assert serial.sha(path) == digest
        settings, trim = entries
        build = serial.read(serial.RAW / build_name / "manifest.json")
        assert build["status"] == "ok" and serial.sha(build["binary"]) == build["binary_sha256"]
        extra = dict(settings["selected_extra"]) if settings["performance_admitted"] else {}
        extra["GAGP_GPU_TRIM_EMPTY_INITIAL_TILES"] = "1" if trim["performance_admitted"] else "0"
        state.update(status="all30_g64", candidate_extra=extra, binary=build["binary"],
                     binary_sha256=build["binary_sha256"],
                     component_decisions={setting_name: settings["performance_admitted"], trim_name: trim["performance_admitted"]})
        serial.save(record, state)
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            stages = {v: serial.Stage(name + "-" + v,
                                     protocol["baseline_binary"] if v == "baseline" else build["binary"],
                                     settings=protocol["baseline_settings"]) for v in ("baseline", "candidate")}
            for stage in stages.values():
                stage.m.update(harness_sha256=state["harness_sha256"], source_build=build_name)
                stage.persist()
        comparisons = []
        for index, task in enumerate(protocol["tasks"]):
            if time.time() >= protocol["development_cutoff_epoch"]:
                raise RuntimeError("Development cutoff reached")
            order = ("baseline", "candidate") if index % 2 == 0 else ("candidate", "baseline")
            for variant in order:
                development.record_search(stages[variant], task, variant,
                                           protocol["development_inputs"][task], {} if variant == "baseline" else extra)
            with serial.GPU_LOCK.open("a") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                rows = settings_followup.compare({v: stages[v].m["jobs"][f"{task}.s0.g64"] for v in stages})
                comparisons.extend(rows)
                serial.save(serial.RAW / (name + "-comparisons.json"), comparisons)
                assert all(r[k] for r in rows for k in ("full_final_ast_equal", "final_descriptors_equal", "champions_equal")), "Composed execution-only trajectory mismatch"
        mean = math.exp(sum(math.log(r["speedup"]) for r in comparisons) / len(protocol["tasks"]))
        state.update(status="requested_phase_complete", finished_epoch=time.time(), task_count=len(protocol["tasks"]),
                     geometric_mean=mean, target_observed_in_development=mean >= 1.3,
                     caveat="All30 single-seed development; not formal3repeat or independent quality proof")
        for stage in stages.values():
            stage.m.update(status="requested_phase_complete", finished_epoch=time.time())
            stage.persist()
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--setting-name", required=True)
    parser.add_argument("--trim-name", required=True)
    parser.add_argument("--build-name", required=True)
    args = parser.parse_args()
    main(args.name, args.setting_name, args.trim_name, args.build_name)
