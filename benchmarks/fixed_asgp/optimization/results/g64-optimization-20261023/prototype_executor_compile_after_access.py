"""Compile real VM caller specializations only after all prior oracle work."""
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
        "private_window_executor_access.cuh", "executor_compile_probe.cu", "access-protocol.json")]
    sources += [Path(__file__), serial.BASE / "closeout_after_formal.py",
                serial.RAW / (formal_name + "-quality-freeze.json")]
    state = dict(pid=os.getpid(), status="waiting_for_cpu_access_oracle", started_epoch=time.time(),
                 deadline_epoch=serial.DEADLINE, prerequisite=prerequisite, formal=formal_name, closeout=closeout_name,
                 input_sha256={str(f): serial.sha(f) for f in sources},
                 scope="Compile-only real VM caller compatibility; no kernel launch, VM parity or speed result")
    serial.save(record, state)
    try:
        while True:
            path = serial.RAW / (prerequisite + "-controller.json")
            previous = serial.read(path) if path.exists() else {}
            if previous.get("status") in ("stopped", "not_run_prerequisite_failure", "budget_not_run"):
                state.update(status="not_run_prerequisite_failure", finished_epoch=time.time())
                return
            if previous.get("status") == "cpu_access_oracle_pass_only" and not Path(f"/proc/{previous['pid']}").exists():
                break
            if time.time() >= serial.DEADLINE - 1800:
                state.update(status="budget_not_run", no_compile=True, finished_epoch=time.time())
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
            assert time.time() < serial.DEADLINE - 1800
            assert all(serial.sha(f) == digest for f, digest in state["input_sha256"].items())
            freeze = serial.read(sources[-1])
            assert serial.sha(freeze["source_provenance"]) == freeze["source_provenance_sha256"]
            provenance = serial.read(freeze["source_provenance"])
            folder = serial.RAW / name
            folder.mkdir()
            copied = folder / "source"
            copied.mkdir()
            for relative, digest in provenance["cpp_source_sha256"].items():
                path = Path(relative)
                assert not path.is_absolute() and path.parts[0] == "cpp" and ".." not in path.parts
                source = serial.ROOT / path
                assert serial.sha(source) == digest, "Official source changed before compile-only snapshot"
                target = copied / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(source.read_bytes())
                assert serial.sha(target) == digest
            for source in sources[:5]:
                target = copied / source.name
                target.write_bytes(source.read_bytes())
                assert serial.sha(target) == state["input_sha256"][str(source)]
            cache = serial.ROOT / "cpp/build/release/CMakeCache.txt"
            values = {}
            for line in cache.read_text().splitlines():
                if not line.startswith(("#", "//")) and "=" in line and ":" in line.split("=", 1)[0]:
                    key, value = line.split("=", 1)
                    values[key.split(":", 1)[0]] = value
            compiler = values["CMAKE_CUDA_COMPILER"]
            host_compiler = values.get("CMAKE_CUDA_HOST_COMPILER")
            assert not host_compiler or Path(host_compiler).is_file()
            arch = values["CMAKE_CUDA_ARCHITECTURES"].split(";")[0].split("-")[0]
            assert arch.isdigit() and Path(compiler).is_file()
            state.update(status="compile_only", compiler=compiler, architecture=arch,
                         host_compiler=host_compiler,
                         cmake_cache_sha256=serial.sha(cache), source_snapshot=str(copied),
                         official_source_sha256=provenance["cpp_source_sha256"], kernels_launched=0)
            serial.save(record, state)
            closeout_after_formal.run_command(state, record, folder, "compiler_version", [compiler, "--version"], 20, owns_lock=False)
            output = folder / "executor-probe.o"
            command = [compiler, "-std=c++17", "-O2", "--device-c", "-gencode=arch=compute_" + arch + ",code=sm_" + arch,
                       "-I" + str(copied), "-I" + str(copied / "cpp/include"),
                       str(copied / "executor_compile_probe.cu"), "-o", str(output)]
            if host_compiler:
                command.extend(["-ccbin", host_compiler])
            closeout_after_formal.run_command(state, record, folder, "compile_probe", command, 480, owns_lock=False)
            closeout_after_formal.run_command(state, record, folder, "symbols", ["nm", "-C", "--defined-only", str(output)], 20, owns_lock=False)
            symbols = (folder / "symbols.log").read_text()
            expected = [f"packed_probe_{flavor}_{layout}_{states}" for flavor in ("bound", "arena", "indexed")
                        for layout in ("aos", "soa") for states in (1, 2)]
            assert all(name in symbols for name in expected) and output.stat().st_size > 1024
            state.update(object_sha256=serial.sha(output), compiled_probe_symbols=expected)
        state.update(status="cuda_executor_compile_probe_pass_only", finished_epoch=time.time(),
                     limitation="No kernel executed; host routing/allocation and VM fuel/type/error/32gen parity/performance still unverified; not1.3x proof")
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
