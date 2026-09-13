"""Capture direct values, errors, payloads, and fuel from the reference GPU core."""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
from pathlib import Path

from gagp_tools.experiments.grammar_migration import sha256, write_json


def capture(reference: Path, reference_build: Path, adapter_build: Path,
            repository: Path, oracle: Path, output: Path, device: int) -> None:
    source_manifest = json.loads((oracle / "manifest.json").read_text())
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=reference, text=True).strip()
    if revision != source_manifest["reference_revision"]:
        raise ValueError("oracle and reference revisions differ")
    if subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=no"], cwd=reference, text=True):
        raise ValueError("reference tracked source changed")
    output.mkdir(parents=True, exist_ok=False)
    sources = ["cpp/src/bench/capture_gpu_results.cu", "cpp/src/bench/migration_snapshot.cpp",
               "cpp/src/bench/migration_snapshot.hpp"]
    for source in [*sources, str(Path(__file__).resolve().relative_to(repository))]:
        destination = output / "source" / source
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(repository / source, destination)
    libraries = [adapter_build / "libgagp_cli_support.a", *[reference_build / name for name in
        ("libgagp_evolution.a", "libgagp_runtime_cpu.a", "libgagp_core.a", "libgagp_gpu.a")]]
    command = ["nvcc", "-std=c++17", "-O3", "-arch=sm_89", "--maxrregcount=64", "-DGAGP_HAS_CUDA=1",
        f'-DGAGP_GPU_FITNESS_SOURCE="{reference / "cpp/src/runtime/gpu/fitness_gpu.cu"}"',
        "-I", str(reference / "cpp/include"), "-I", str(output / "source/cpp/src/bench"),
        str(output / "source" / sources[0]), str(output / "source" / sources[1]), "-o", str(output / "capture"),
        "-Xlinker", "--start-group", *map(str, libraries), "-Xlinker", "--end-group",
        "-lcudadevrt", "-lcudart_static", "-lrt", "-lpthread", "-ldl"]
    manifest = {"version": "migration-direct-gpu-oracle-v1", "reference_revision": revision,
        "oracle_manifest_sha256": sha256(oracle / "manifest.json"), "device": device,
        "sources": {s: sha256(output / "source" / s) for s in sources},
        "libraries": {str(p): sha256(p) for p in libraries}, "compile_command": command, "captures": []}
    write_json(output / "manifest.json", manifest)
    result = subprocess.run(command, capture_output=True, text=True)
    (output / "build.log").write_text(result.stdout + result.stderr)
    result.check_returncode()
    manifest["binary_sha256"] = sha256(output / "capture")
    write_json(output / "manifest.json", manifest)
    for entry in source_manifest["captures"]:
        source = oracle / entry["output"]
        if sha256(source) != entry["output_sha256"]:
            raise ValueError("source oracle capture changed")
        destination = output / entry["output"]
        command = [str(output / "capture"), str(source), str(destination), str(device)]
        result = subprocess.run(command, capture_output=True, text=True)
        write_json(destination.with_suffix(".process.json"), {"command": command,
            "returncode": result.returncode, "stdout": result.stdout, "stderr": result.stderr})
        result.check_returncode()
        original = {r["ordinal"]: r for r in map(json.loads, source.read_text().splitlines()) if r["kind"] == "execution"}
        observed = [json.loads(line) for line in destination.read_text().splitlines()]
        if len(observed) != len(original) or {r["ordinal"] for r in observed} != set(original):
            raise ValueError("direct GPU oracle lost execution rows")
        differences = []
        for row in observed:
            cpu = original[row["ordinal"]]
            validate_probe_phases(cpu, gpu=False)
            validate_probe_phases(row, gpu=True)
            changed = []
            for phase in ("result", "at_probe_cap", "at_boundary", "below_boundary"):
                if phase not in row or phase not in cpu:
                    continue
                if not row[phase]["ordinary_core_match"]:
                    raise ValueError("diagnostic core differed from ordinary evaluator")
                if any(row[phase].get(key) != cpu[phase].get(key) for key in ("error", "value")):
                    changed.append(phase)
            if row["first_non_timeout_fuel"] != cpu["first_non_timeout_fuel"]:
                changed.append("first_non_timeout_fuel")
            if changed:
                differences.append({"ordinal": row["ordinal"], "fields": changed})
        manifest["captures"].append({"input": str(source), "input_sha256": sha256(source),
            "output": str(destination), "output_sha256": sha256(destination), "executions": len(observed),
            "cpu_differences": differences})
        write_json(output / "manifest.json", manifest)
        print(entry["output"], len(observed), "executions", len(differences), "CPU differences", flush=True)


def validate_probe_phases(row: dict, *, gpu: bool) -> None:
    boundary = row["first_non_timeout_fuel"]
    required = {"result", "at_probe_cap"}
    if boundary is not None:
        if type(boundary) is not int or not 0 <= boundary <= row["probe_cap"]:
            raise ValueError("invalid fuel boundary")
        required.add("at_boundary")
        if boundary > 0:
            required.add("below_boundary")
    present = set(row) & {"result", "at_probe_cap", "at_boundary", "below_boundary"}
    if present != required:
        raise ValueError("incomplete or unexpected probe phases")
    if gpu and any(row[phase].get("ordinary_core_match") is not True for phase in required):
        raise ValueError("diagnostic core differed from ordinary evaluator")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("reference", "reference-build", "adapter-build", "oracle", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument("--device", type=int, required=True)
    args = parser.parse_args()
    capture(args.reference.resolve(), args.reference_build.resolve(), args.adapter_build.resolve(),
            args.repository.resolve(), args.oracle.resolve(), args.output.resolve(), args.device)


if __name__ == "__main__":
    main()
