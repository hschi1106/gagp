"""Check isolated packed root/child/local-leaf access after original oracles."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import time
import traceback

import closeout_after_formal
import serial


def main(name, prerequisite, formal_name, closeout_name):
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists()
    prototype = serial.BASE / "prototype-private-frames"
    sources = [prototype / n for n in ("private_window_frames_gpu.hpp", "private_window_frame_views.hpp",
        "private_window_executor_access.cuh", "access_oracle.cpp", "access-protocol.json")]
    sources += [serial.ROOT / "cpp/include/gagp/core/value.hpp", Path(__file__),
                serial.BASE / "closeout_after_formal.py"]
    state = dict(pid=os.getpid(), status="waiting_for_original_cuda_oracle", started_epoch=time.time(),
                 deadline_epoch=serial.DEADLINE, prerequisite=prerequisite,
                 formal=formal_name, closeout=closeout_name,
                 input_sha256={str(f): serial.sha(f) for f in sources},
                 scope="CPU root/child/local-leaf access only; CUDA accessor, VM and speed unverified")
    serial.save(record, state)
    try:
        while True:
            path = serial.RAW / (prerequisite + "-controller.json")
            previous = serial.read(path) if path.exists() else {}
            if previous.get("status") in ("stopped", "not_run_prerequisite_failure", "budget_not_run"):
                state.update(status="not_run_prerequisite_failure", finished_epoch=time.time())
                return
            if previous.get("status") == "cuda_storage_oracle_pass_only" and not Path(f"/proc/{previous['pid']}").exists():
                break
            if time.time() >= serial.DEADLINE - 900:
                state.update(status="budget_not_run", no_tests=True, finished_epoch=time.time())
                return
            time.sleep(15)
        formal = serial.read(serial.RAW / (formal_name + "-controller.json"))
        closeout = serial.read(serial.RAW / (closeout_name + "-controller.json"))
        assert closeout["status"] == "artifacts_ready_manual_review_pending"
        assert not Path(f"/proc/{closeout['pid']}").exists()
        manifests = [serial.read(f) for f in serial.RAW.glob(formal_name + "*/manifest.json")]
        closeout_after_formal.assert_quiescent(formal, manifests, lambda pid: Path(f"/proc/{pid}").exists())
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert time.time() < serial.DEADLINE - 900
            assert all(serial.sha(f) == digest for f, digest in state["input_sha256"].items())
            folder = serial.RAW / name
            folder.mkdir()
            copied = folder / "source"
            (copied / "gagp/core").mkdir(parents=True)
            for source in sources[:5]:
                (copied / source.name).write_bytes(source.read_bytes())
            (copied / "gagp/core/value.hpp").write_bytes(sources[5].read_bytes())
            state.update(status="cpu_access_oracles_running", sources=str(copied),
                         cuda_accessor_compiled=False, vm_integrated=False)
            serial.save(record, state)
            for label, flags in [("optimized", ["-O2", "-Wall", "-Wextra", "-Werror"]),
                                 ("sanitized", ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"])]:
                binary = folder / ("access-" + label)
                command = ["g++", "-std=c++17", *flags, "-I" + str(copied),
                           str(copied / "access_oracle.cpp"), "-o", str(binary)]
                closeout_after_formal.run_command(state, record, folder, label + "-build", command, 120, owns_lock=False)
                closeout_after_formal.run_command(state, record, folder, label + "-run", [str(binary)], 20, owns_lock=False)
                state["steps"][label + "-run"]["binary_sha256"] = serial.sha(binary)
                serial.save(record, state)
        state.update(status="cpu_access_oracle_pass_only", finished_epoch=time.time(),
                     limitation="CUDA access specialization, full VM/fuel/errors/replay/32generation parity and speed remain unverified; not a1.3x-goal proof")
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--prerequisite", required=True)
    parser.add_argument("--formal", required=True)
    parser.add_argument("--closeout", required=True)
    args = parser.parse_args()
    main(args.name, args.prerequisite, args.formal, args.closeout)
