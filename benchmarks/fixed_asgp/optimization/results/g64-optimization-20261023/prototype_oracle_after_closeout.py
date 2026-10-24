"""Run isolated CPU storage oracles only after formal timing and closeout exit."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import subprocess
import time
import traceback

import closeout_after_formal
import serial


def main(name, formal_name, closeout_name):
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists()
    prototype = serial.BASE / "prototype-private-frames"
    inputs = [prototype / "private_window_frames_gpu.hpp", prototype / "storage_oracle.cpp",
              prototype / "protocol-draft.json", serial.ROOT / "cpp/include/gagp/core/value.hpp", Path(__file__)]
    state = dict(pid=os.getpid(), status="waiting_for_official_closeout", started_epoch=time.time(),
                 deadline_epoch=serial.DEADLINE, formal=formal_name, closeout=closeout_name,
                 input_sha256={str(f): serial.sha(f) for f in inputs},
                 scope="CPU storage bit/type/lazy-access checks only; no GPU integration or speed claim")
    serial.save(record, state)
    try:
        while True:
            path = serial.RAW / (closeout_name + "-controller.json")
            closeout = serial.read(path) if path.exists() else {}
            if closeout.get("status") == "stopped":
                state.update(status="not_run_prerequisite_failure", finished_epoch=time.time())
                return
            if closeout.get("status") == "artifacts_ready_manual_review_pending" and not Path(f"/proc/{closeout['pid']}").exists():
                break
            if time.time() >= serial.DEADLINE - 600:
                state.update(status="budget_not_run", no_tests=True, finished_epoch=time.time())
                return
            time.sleep(15)
        formal = serial.read(serial.RAW / (formal_name + "-controller.json"))
        manifests = [serial.read(f) for f in serial.RAW.glob(formal_name + "*/manifest.json")]
        closeout_after_formal.assert_quiescent(formal, manifests, lambda pid: Path(f"/proc/{pid}").exists())
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert all(serial.sha(f) == digest for f, digest in state["input_sha256"].items())
            folder = serial.RAW / name
            folder.mkdir()
            copied = folder / "source"
            (copied / "gagp/core").mkdir(parents=True)
            for source, destination in [(inputs[0], copied / inputs[0].name), (inputs[1], copied / "oracle.cpp"),
                                        (inputs[2], copied / inputs[2].name), (inputs[3], copied / "gagp/core/value.hpp")]:
                destination.write_bytes(source.read_bytes())
            state.update(status="cpu_oracles_running", sources=str(copied))
            serial.save(record, state)
            for label, flags in [("optimized", ["-O2", "-Wall", "-Wextra", "-Werror"]),
                                 ("sanitized", ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"])]:
                binary = folder / ("oracle-" + label)
                command = ["g++", "-std=c++17", *flags, "-I" + str(copied), str(copied / "oracle.cpp"), "-o", str(binary)]
                closeout_after_formal.run_command(state, record, folder, label + "-build", command, 120, owns_lock=False)
                closeout_after_formal.run_command(state, record, folder, label + "-run", [str(binary)], 20, owns_lock=False)
                state["steps"][label + "-run"]["binary_sha256"] = serial.sha(binary)
                serial.save(record, state)
        state.update(status="cpu_oracle_pass_only", finished_epoch=time.time(),
                     limitation="Not a CUDA, integration, parity, speed, quality or1.3x-goal verification; original deadline unchanged")
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--formal", required=True)
    parser.add_argument("--closeout", required=True)
    args = parser.parse_args()
    main(args.name, args.formal, args.closeout)
