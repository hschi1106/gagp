"""Capture unchanged native semantic tests against immutable baseline libraries."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path


TEST_SOURCES = (
    "cpp/tests/evolution/test_structured_semantics.cpp",
    "cpp/tests/runtime/test_asgp_semantics.cpp",
    "cpp/tests/parity/test_fitness_cpu_gpu_parity.cpp",
)
VERIFIER_SOURCES = (
    "cpp/tests/evolution/test_ast_verify_asgp.cpp",
    "cpp/tests/evolution/test_ast_verify_binders.cpp",
    "cpp/tests/runtime/test_bytecode_verify.cpp",
)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def capture(reference: Path, reference_build: Path, adapter_build: Path,
            repository: Path, output: Path, device: int, *, boundaries: bool = False,
            gpu_observations: bool = False, verifiers: bool = False) -> None:
    if boundaries and verifiers:
        raise ValueError("choose either boundary or verifier capture")
    output.mkdir(parents=True, exist_ok=False)
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=reference, text=True).strip()
    changes = subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=no"],
                                      cwd=reference, text=True)
    if changes:
        raise ValueError("reference tracked files must be immutable")
    libraries = [reference_build / name for name in (
        "libgagp_evolution.a", "libgagp_runtime_cpu.a", "libgagp_core.a", "libgagp_gpu.a")]
    # The CLI archive supplies only codecs; all compiler/runtime work uses the
    # original reference archives. Snapshot support is built from recorded source.
    codec = adapter_build / "libgagp_cli_support.a"
    sources = [repository / "cpp/src/bench/capture_baseline_tests.cpp",
               repository / "cpp/src/bench/migration_snapshot.cpp",
               repository / "cpp/src/bench/migration_snapshot.hpp"]
    if boundaries:
        sources.append(repository / "cpp/src/bench/migration_boundary_cases.hpp")
    frozen_repository = output / "adapter-source"
    for source in [*sources, Path(__file__).resolve()]:
        destination = frozen_repository / source.relative_to(repository)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
    manifest = {"version": "migration-native-oracle-v1", "reference_revision": revision,
                "device": device, "boundaries": boundaries, "gpu_observations": gpu_observations,
                "verifiers": verifiers,
                "libraries": {str(p): digest(p) for p in [*libraries, codec]},
                "adapter_sources": {str(p): digest(p) for p in sources}, "captures": []}
    manifest["capture_tool_sha256"] = digest(Path(__file__).resolve())
    sources = [frozen_repository / p.relative_to(repository) for p in sources]
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    selected = VERIFIER_SOURCES if verifiers else (TEST_SOURCES[1],) if boundaries else TEST_SOURCES
    for index, relative in enumerate(selected):
        source = reference / relative
        executable = output / f"capture-{index}"
        command = ["c++", "-std=c++17", "-O3", "-DNDEBUG", "-DGAGP_HAS_CUDA=1",
            f'-DGAGP_ORACLE_SOURCE="{source}"', "-I", str(reference / "cpp/include"),
            "-I", str(frozen_repository / "cpp/src/bench"), str(sources[0]), str(sources[1]),
            "-o", str(executable), "-Wl,--start-group", str(codec), *map(str, libraries),
            "-Wl,--end-group", "-L/usr/local/cuda/targets/x86_64-linux/lib", "-lcudadevrt",
            "-lcudart_static", "-lrt", "-lpthread", "-ldl"]
        if boundaries:
            command.insert(1, "-DGAGP_EXTRA_BOUNDARIES=1")
        if gpu_observations:
            command.insert(1, "-DGAGP_CAPTURE_GPU=1")
        compiled = subprocess.run(command, capture_output=True, text=True)
        (output / f"compile-{index}.log").write_text(compiled.stdout + compiled.stderr)
        (output / f"compile-{index}.json").write_text(json.dumps(command, indent=2) + "\n")
        compiled.check_returncode()
        raw = output / f"capture-{index}.jsonl"
        result = subprocess.run([str(executable), str(raw)],
            env=dict(os.environ, GAGP_CUDA_DEVICE=str(device)), cwd=reference, capture_output=True, text=True)
        (output / f"capture-{index}.log").write_text(result.stdout + result.stderr)
        result.check_returncode()
        if "SKIP" in result.stdout or "SKIP" in result.stderr:
            raise RuntimeError("reference test skipped execution; capture is incomplete")
        counts = {}
        for line in raw.read_text().splitlines():
            row = json.loads(line)
            counts[row["kind"]] = counts.get(row["kind"], 0) + 1
        manifest["captures"].append({"source": relative, "source_sha256": digest(source),
            "executable_sha256": digest(executable), "output": raw.name,
            "output_sha256": digest(raw), "counts": counts})
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--reference-build", type=Path, required=True)
    parser.add_argument("--adapter-build", type=Path, required=True)
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--device", type=int, required=True)
    parser.add_argument("--boundaries", action="store_true")
    parser.add_argument("--gpu-observations", action="store_true")
    parser.add_argument("--verifiers", action="store_true")
    args = parser.parse_args()
    capture(args.reference.resolve(), args.reference_build.resolve(), args.adapter_build.resolve(),
            args.repository.resolve(), args.output.resolve(), args.device,
            boundaries=args.boundaries, gpu_observations=args.gpu_observations, verifiers=args.verifiers)


if __name__ == "__main__":
    main()
