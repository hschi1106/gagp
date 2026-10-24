"""Preserve failed implementation and restore only its exact baseline CPP paths."""
import fcntl
import os
from pathlib import Path
import shutil
import subprocess
import time

import serial


def main():
    protocol = serial.read(serial.BASE / "protocol.json")
    build = serial.read(serial.RAW / "build-attempt1/manifest.json")
    assert build["status"] == "failed"
    for field in ("pid", "child_pid"):
        assert not Path(f"/proc/{build[field]}").exists(), "Failed build still has a live process"
    folder = serial.RAW / "rejected-trim1"
    assert not folder.exists(), "Preserve prior rejection records"
    with serial.GPU_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        assert all(serial.sha(serial.ROOT / p) == h for p, h in build["source_sha256"].items())
        folder.mkdir()
        changed = [p for p, h in build["source_sha256"].items()
                   if h != protocol["baseline_source_sha256"][p]]
        assert len(changed) == 5
        binaries = {}
        for name in ("gagp_fixed_asgp_bench", "gagp_test_gpu_phase_population", "gagp_test_extended_payload_gpu"):
            path = serial.ROOT / "cpp/build/release" / name
            shutil.copy2(path, folder / name)
            binaries[name] = serial.sha(folder / name)
        for path in changed:
            destination = folder / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(serial.ROOT / path, destination)
        proof = dict(epoch=time.time(), status="rejected_native_failed_not_applicable", changed_paths=changed,
                     preserved_binary_sha256=binaries,
                     reason="Arena cases use contiguous floor(case_count*logical_lane/logical_lanes) partitions, not prefix/strided lanes. C19..200 has no provably empty 512-lane warp; prefix trimming skipped real cases. Native tests correctly rejected it.",
                     restore_commit=protocol["baseline_commit"], no_scientific_candidate_searches=True)
        serial.save(folder / "rejection.json", proof)
        for path in changed:
            original = subprocess.check_output(["git", "show", protocol["baseline_commit"] + ":" + path], cwd=serial.ROOT)
            (serial.ROOT / path).write_bytes(original)
            assert serial.sha(serial.ROOT / path) == protocol["baseline_source_sha256"][path]
        assert all(serial.sha(serial.ROOT / p) == h for p, h in protocol["baseline_source_sha256"].items())
        proof.update(status="rejected_and_baseline_source_restored", finished_epoch=time.time(),
                     all_451_cpp_restored=True)
        serial.save(folder / "rejection.json", proof)
        print(proof)


if __name__ == "__main__":
    main()
