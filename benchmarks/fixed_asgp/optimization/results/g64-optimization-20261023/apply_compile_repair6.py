"""Preserve failed attempt5 and repair two nonexistent-field references."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import shutil
import time
import traceback

import build_candidate_v3
import serial


def main(name, checks_name):
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists() and not (serial.RAW / "build-attempt6").exists()
    state = dict(pid=os.getpid(), status="waiting_for_guards", started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 optional_build_cutoff=serial.START+16200, source_not_changed=True)
    serial.save(record, state)
    try:
        while True:
            path = serial.RAW / checks_name / "manifest.json"
            checks = serial.read(path) if path.exists() else {}
            if checks.get("status") == "failed":
                raise RuntimeError("Python guards failed")
            if checks.get("status") == "ok" and not Path(f"/proc/{checks['pid']}").exists():
                break
            assert time.time() < state["optional_build_cutoff"]
            time.sleep(10)
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert time.time() < state["optional_build_cutoff"]
            failed = serial.read(serial.RAW / "build-attempt5/manifest.json")
            assert failed["status"] == "failed" and "binary" not in failed
            for pid in (failed["pid"], failed["child_pid"]):
                assert not Path(f"/proc/{pid}").exists(), "Old compiler/controller still lives"
            for old in ("apply-private5c", "private5c", "geometry5", "asgp-evidence-readback0"):
                prior = serial.read(serial.RAW / (old + "-controller.json"))
                assert not Path(f"/proc/{prior['pid']}").exists()
            assert not list(serial.RAW.glob("*-quality-freeze.json"))
            assert all(serial.sha(serial.ROOT/p) == digest for p, digest in failed["source_sha256"].items())
            archive = serial.RAW / "rejected-build5"
            archive.mkdir()
            paths = ("cpp/src/runtime/gpu/device/kernels.cuh", "cpp/src/runtime/gpu/fitness_gpu.cu")
            for path in paths:
                target = archive / path
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(serial.ROOT / path, target)
            serial.save(archive / "failure.json", dict(build_manifest=str(serial.RAW / "build-attempt5/manifest.json"),
                build_manifest_sha256=serial.sha(serial.RAW / "build-attempt5/manifest.json"),
                reason="private_global_offsets is host normalization state, not a DRegionWorkspace member",
                executable_produced=False, candidate_search_samples=0,
                full_failed_source_delta=str(serial.RAW / "build-attempt5/source-delta/tracked.patch")))
            kernel, host = [serial.ROOT / p for p in paths]
            text = kernel.read_text();old = "    workspace.private_global_offsets = true;\n"
            assert text.count(old) == 1;kernel.write_text(text.replace(old, ""))
            text = host.read_text();old = "      region_workspace.private_global_offsets && region_workspace.private_warp_copy &&\n"
            assert text.count(old) == 1
            host.write_text(text.replace(old, "      private_global_offsets && region_workspace.private_warp_copy &&\n"))
            state.update(status="building_and_native_checks", source_not_changed=False, repaired_epoch=time.time(),
                         archive=str(archive), changed_source_sha256={p:serial.sha(serial.ROOT/p) for p in paths})
            serial.save(record, state)
        build_candidate_v3.run("build-attempt6")
        state.update(status="requested_phase_complete", finished_epoch=time.time())
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--checks-name", required=True)
    args = parser.parse_args()
    main(args.name, args.checks_name)
