"""Run the isolated CUDA storage oracle after official work and CPU oracles."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import time
import traceback

import closeout_after_formal
import serial


def main(name, cpu_name, formal_name, closeout_name):
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists()
    prototype = serial.BASE / "prototype-private-frames"
    sources = [prototype / "private_window_frames_gpu.hpp", prototype / "storage_oracle.cu",
               prototype / "protocol-draft.json", serial.ROOT / "cpp/include/gagp/core/value.hpp",
               Path(__file__), serial.BASE / "closeout_after_formal.py"]
    state = dict(pid=os.getpid(), status="waiting_for_cpu_oracles", started_epoch=time.time(),
                 deadline_epoch=serial.DEADLINE, cpu=cpu_name, formal=formal_name, closeout=closeout_name,
                 input_sha256={str(f): serial.sha(f) for f in sources},
                 scope="CUDA storage/address/word-ownership/canary checks only; not VM/parity/performance validation")
    serial.save(record, state)
    try:
        while True:
            path = serial.RAW / (cpu_name + "-controller.json")
            cpu = serial.read(path) if path.exists() else {}
            if cpu.get("status") in ("stopped", "not_run_prerequisite_failure", "budget_not_run"):
                state.update(status="not_run_prerequisite_failure", finished_epoch=time.time())
                return
            if cpu.get("status") == "cpu_oracle_pass_only" and not Path(f"/proc/{cpu['pid']}").exists():
                break
            if time.time() >= serial.DEADLINE - 600:
                state.update(status="budget_not_run", no_tests=True, finished_epoch=time.time())
                return
            time.sleep(15)
        formal = serial.read(serial.RAW / (formal_name + "-controller.json"))
        closeout = serial.read(serial.RAW / (closeout_name + "-controller.json"))
        assert closeout["status"] == "artifacts_ready_manual_review_pending" and not Path(f"/proc/{closeout['pid']}").exists()
        manifests = [serial.read(f) for f in serial.RAW.glob(formal_name + "*/manifest.json")]
        closeout_after_formal.assert_quiescent(formal, manifests, lambda pid: Path(f"/proc/{pid}").exists())
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert all(serial.sha(f) == digest for f, digest in state["input_sha256"].items())
            cache = serial.ROOT / "cpp/build/release/CMakeCache.txt"
            values = {}
            for line in cache.read_text().splitlines():
                if not line.startswith(("#", "//")) and "=" in line and ":" in line.split("=", 1)[0]:
                    key, value = line.split("=", 1)
                    values[key.split(":", 1)[0]] = value
            compiler = values["CMAKE_CUDA_COMPILER"]
            architecture = values["CMAKE_CUDA_ARCHITECTURES"].split(";")[0].split("-")[0]
            assert architecture.isdigit() and Path(compiler).is_file(), "Inspect actual installed CUDA toolchain"
            folder = serial.RAW / name
            folder.mkdir()
            copied = folder / "source"
            (copied / "gagp/core").mkdir(parents=True)
            for source, target in [(sources[0], copied / sources[0].name), (sources[1], copied / "oracle.cu"),
                                   (sources[2], copied / sources[2].name), (sources[3], copied / "gagp/core/value.hpp")]:
                target.write_bytes(source.read_bytes())
            state.update(status="cuda_storage_oracle_running", cmake_cache_sha256=serial.sha(cache),
                         compiler=compiler, architecture=architecture, sources=str(copied))
            serial.save(record, state)
            closeout_after_formal.run_command(state, record, folder, "compiler_version", [compiler, "--version"], 20, owns_lock=False)
            binary = folder / "oracle"
            command = [compiler, "-std=c++17", "-O2", "-gencode=arch=compute_" + architecture + ",code=sm_" + architecture,
                       "-gencode=arch=compute_" + architecture + ",code=compute_" + architecture,
                       "-I" + str(copied), str(copied / "oracle.cu"), "-o", str(binary)]
            closeout_after_formal.run_command(state, record, folder, "cuda_build", command, 180, owns_lock=False)
            closeout_after_formal.run_command(state, record, folder, "cuda_run", [str(binary)], 60, owns_lock=False)
            state["binary_sha256"] = serial.sha(binary)
        state.update(status="cuda_storage_oracle_pass_only", finished_epoch=time.time(),
                     limitation="Not integrated into evaluation/reproduction; no speedup or1.3x-goal claim; original deadline unchanged")
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--cpu", required=True)
    parser.add_argument("--formal", required=True)
    parser.add_argument("--closeout", required=True)
    args = parser.parse_args()
    main(args.name, args.cpu, args.formal, args.closeout)
