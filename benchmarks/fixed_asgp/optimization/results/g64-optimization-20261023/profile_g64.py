"""Nsight Systems diagnosis on fixed old training, never headline timing."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import subprocess
import time
import traceback

import development_v2 as development
import serial


TASKS = ("is_sorted", "double_letters", "median", "tribonacci")


def main(name):
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists(), "Keep all prior attempts"
    protocol = serial.read(serial.BASE / "protocol.json")
    state = dict(pid=os.getpid(), status="waiting_for_lock", started_epoch=time.time(),
                 deadline_epoch=serial.DEADLINE, tasks=TASKS,
                 scope="nsys only, old TRAINING P1024/G64; exclude profiled times from ratios",
                 harness_sha256={str(p): serial.sha(p) for p in
                                 [Path(__file__), serial.BASE / "serial.py",
                                  serial.BASE / "development_v2.py", serial.BASE / "protocol.json"]})
    serial.save(record, state)
    try:
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert time.time() < protocol["development_cutoff_epoch"]
            assert serial.sha(protocol["baseline_binary"]) == protocol["baseline_binary_sha256"]
            stage = serial.Stage(name, protocol["baseline_binary"], settings=protocol["baseline_settings"])
            stage.m.update(harness_sha256=state["harness_sha256"], diagnostic_only=True)
            stage.persist()
        state["status"] = "profiling"
        serial.save(record, state)
        for task in TASKS:
            assert time.time() < protocol["development_cutoff_epoch"]
            spec = protocol["development_inputs"][task]
            tag = task + ".s0.g64.profile"
            output, trace = stage.out / (tag + ".json"), stage.out / tag
            job = stage.run(tag, ["measure", spec["source"], spec["prepared"], spec["grammar"],
                                  1024, "search_gpu_overlap", output],
                            dict(GAGP_SEARCH_GENERATIONS="64", GAGP_BM_SEED="0",
                                 GAGP_BM_CASE_COUNT=str(spec["train_count"])), timeout=1200,
                            prefix=["nsys", "profile", "--trace=cuda,nvtx", "--sample=none",
                                    "--cpuctxsw=none", "-o", str(trace)], reserve=14400)
            with serial.GPU_LOCK.open("a") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                job.update(task=task, population=1024, generations=64,
                           output=str(output), scope="diagnostic_profile_only")
                stage.persist()
                assert job["status"] == "ok", "Keep failed diagnostic; no retry"
                development.validate(serial.read(output), spec["train_count"])
                artifact = Path(str(trace) + ".nsys-rep")
                assert artifact.exists()
                job.update(output_sha256=serial.sha(output), trace=str(artifact),
                           trace_sha256=serial.sha(artifact))
                for report in ("cuda_gpu_kern_sum", "cuda_api_sum", "nvtx_sum"):
                    target = stage.out / (tag + "." + report + ".csv")
                    command = ["nsys", "stats", "--report", report, "--format", "csv",
                               "--force-export=true", str(artifact)]
                    with target.open("x") as stream:
                        child = subprocess.Popen(command, cwd=serial.ROOT, stdout=stream,
                                                 stderr=subprocess.STDOUT, start_new_session=True)
                        state.update(child_pid=child.pid, current_command=command)
                        serial.save(record, state)
                        result = child.wait(timeout=min(600, max(1, protocol["development_cutoff_epoch"] - time.time())))
                    assert result == 0, "Keep failed stats export"
                    job.setdefault("stats", {})[report] = dict(path=str(target), sha256=serial.sha(target))
                    stage.persist()
        stage.m.update(status="requested_phase_complete", finished_epoch=time.time())
        stage.persist()
        state.update(status="requested_phase_complete", finished_epoch=time.time())
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    main(parser.parse_args().name)
