"""Capture the selected CPP/ASGP/overlay/compiler inputs before fresh data."""
from pathlib import Path
import subprocess
import time

import serial



def external_identity(external):
    result = subprocess.run(["git", "-C", str(external), "rev-parse", "HEAD"], cwd=serial.ROOT,
                            capture_output=True, text=True)
    if result.returncode == 0:
        status = subprocess.check_output(["git", "-C", str(external), "status", "--short"], cwd=serial.ROOT, text=True)
        return dict(kind="git_worktree", git_head=result.stdout.strip(), worktree_status=status)
    if "not a git repository" not in result.stderr:
        raise RuntimeError("ASGP Git provenance probe failed: " + result.stderr.strip())
    return dict(kind="directory_without_git_metadata", git_head=None, worktree_status=None,
                git_probe_error=result.stderr.strip(),
                limitation="Exact source/overlay file hashes are recorded; no upstream Git revision can be inferred")


def collect(build_name):
    build_path = serial.RAW / build_name / "manifest.json"
    build = serial.read(build_path)
    assert build["status"] == "ok" and not Path(f"/proc/{build['pid']}").exists()
    assert serial.sha(build["binary"]) == build["binary_sha256"]
    assert all(serial.sha(serial.ROOT / p) == digest for p, digest in build["source_sha256"].items())
    cache = serial.ROOT / "cpp/build/release/CMakeCache.txt"
    values = {}
    for line in cache.read_text().splitlines():
        if line and not line.startswith(("#", "//")) and "=" in line and ":" in line.split("=", 1)[0]:
            key, value = line.split("=", 1)
            values[key.split(":", 1)[0]] = value
    external = Path(values["GAGP_ASGP_SOURCE_DIR"]).resolve()
    assert external.is_dir() and values["CMAKE_BUILD_TYPE"] == "Release"
    # The external source is not part of this worktree's CPP map. Record it
    # explicitly, including local changes, rather than implying upstream paper
    # source equivalence. Fresh ASGP controls use the selected rebuilt binary.
    files = sorted(p for p in (external / "src").rglob("*") if p.is_file())
    assert files and all(p.stat().st_mtime <= build["started_epoch"] for p in files), "ASGP source changed after selected build began"
    overlay = serial.ROOT / "cpp/build/release/asgp-list-combine-v1"
    generated = sorted(p for p in overlay.rglob("*") if p.is_file())
    assert values.get("GAGP_ASGP_LIST_COMBINE_EXTENSION") == "ON" and len(generated) >= 3
    assert all(p.stat().st_mtime <= build["finished_epoch"] for p in generated)
    external_metadata = external_identity(external)
    external_head = external_metadata["git_head"]
    external_status = external_metadata["worktree_status"]
    flags = [serial.ROOT / "cpp/build/release/external-asgp/CMakeFiles/asgp_core.dir/flags.make",
             serial.ROOT / "cpp/build/release/CMakeFiles/gagp_fixed_asgp_bench.dir/flags.make",
             serial.ROOT / "cpp/build/release/CMakeFiles/gagp_gpu.dir/flags.make", cache]
    assert all(p.exists() for p in flags)
    return dict(observed_epoch=time.time(), source_build=build_name, build_manifest=str(build_path),
                build_manifest_sha256=serial.sha(build_path), selected_binary=build["binary"],
                selected_binary_sha256=build["binary_sha256"], cpp_source_sha256=build["source_sha256"],
                asgp_source_root=str(external), asgp_git_head=external_head, asgp_worktree_status=external_status,
                asgp_directory_metadata=external_metadata,
                asgp_source_sha256={str(p): serial.sha(p) for p in files},
                asgp_overlay_sha256={str(p): serial.sha(p) for p in generated},
                build_input_sha256={str(p): serial.sha(p) for p in flags},
                asgp_list_combine_extension=True, cmake_build_type=values["CMAKE_BUILD_TYPE"],
                cuda_architectures=values.get("CMAKE_CUDA_ARCHITECTURES"),
                scope="Selected rebuilt binary plus exact source/overlay/config inputs. Baseline GPU binary retains its original bootstrap provenance; no paper-replication claim.")
