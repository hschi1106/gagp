"""Collect evolving population distributions separately from timing trials."""
from __future__ import annotations

import argparse
import json
import os
import subprocess
from pathlib import Path

from gagp_tools.experiments.grammar_migration import (
    canonical_hash, sha256, validate_manifest, workload_modes, write_json,
)


def validate_host_decisions(population: dict, gpu_reproduction: bool) -> None:
    counts = population.get("host_operator_decisions", {})
    if not counts or any(type(count) is not int or count < 0 for count in counts.values()):
        raise ValueError("missing or invalid host operator decision counts")
    size = population["population_size"]
    if gpu_reproduction:
        if sum(count for event, count in counts.items() if event.startswith("pack.decode.")) != size:
            raise ValueError("GPU host decode decisions do not account for every child")
    elif counts.get("backend.mutation.eligible") != size or not (
            0 <= counts.get("backend.mutation.selected", 0) <= size):
        raise ValueError("CPU mutation decisions do not account for every child")


def validate_gpu_decisions(population: dict) -> None:
    counts = population.get("gpu_kernel_decisions", {})
    if not counts or any(type(count) is not int or count < 0 for count in counts.values()):
        raise ValueError("missing or invalid GPU kernel decision counts")
    size = population["population_size"]
    physical_children = 2 * ((size + 1) // 2)
    for events in (("variation.assembled", "variation.parent_fallback"),
                   ("metadata.valid", "metadata.invalid"),
                   ("mutation.none", "mutation.subtree", "mutation.constant"),
                   ("output.valid", "output.invalid")):
        if sum(counts.get("gpu_kernel." + event, 0) for event in events) != physical_children:
            raise ValueError("GPU kernel decisions do not account for every physical child")
    if size == physical_children and counts.get("gpu_kernel.output.invalid", 0) != (
            population["host_operator_decisions"].get("pack.decode.device_invalid", 0)):
        raise ValueError("GPU invalid output count differs from host decode decisions")


def capture(workloads: Path, adapter_manifest: Path, root: Path, output: Path, device: int) -> None:
    matrix = json.loads(workloads.read_text())
    validate_manifest(matrix, root)
    adapter = json.loads(adapter_manifest.read_text())
    host_probes = adapter.get("host_operator_probes", False)
    gpu_probes = adapter.get("gpu_kernel_probes", False)
    binaries = {variant: adapter_manifest.parent / variant for variant in ("plain", "instrumented")}
    for variant, binary in binaries.items():
        if sha256(binary) != adapter["builds"][variant]["sha256"]:
            raise ValueError("statistics adapter binary changed")
    output.mkdir(parents=True, exist_ok=False)
    manifest = {"version": "migration-evolution-statistics-capture-v1",
        "reference_revision": adapter["reference_revision"], "workloads_sha256": sha256(workloads),
        "adapter_manifest_sha256": sha256(adapter_manifest), "device": device,
        "host_operator_probes": host_probes,
        "gpu_kernel_probes": gpu_probes,
        "status": "capture in progress", "captures": []}
    write_json(output / "manifest.json", manifest)
    for workload in matrix["workloads"]:
        if workload.get("measurement", "evolution") != "evolution":
            continue
        options = dict(zip(workload["args"][::2], workload["args"][1::2]))
        generations, size = int(options["--generations"]), int(options["--population-size"])
        case_count = len(json.loads((root / workload["cases"]["path"]).read_text())["cases"])
        for mode, mode_options in workload_modes(workload, matrix["modes"]).items():
            mode_flags = dict(zip(mode_options[::2], mode_options[1::2]))
            directory = output / workload["id"] / mode
            directory.mkdir(parents=True)
            observed, commands, hashes = {}, {}, {}
            for variant, binary in binaries.items():
                path = directory / f"{variant}.json"
                command = [str(binary), "--snapshot", str((root / workload["snapshot"]["path"]).resolve()),
                    "--cases", str((root / workload["cases"]["path"]).resolve()), *workload["args"],
                    *mode_options, "--out-json", str(path)]
                if workload.get("grammar"):
                    command.extend(["--grammar-definition", str((root / workload["grammar"]["path"]).resolve())])
                result = subprocess.run(command, capture_output=True, text=True, cwd=root,
                                        env=dict(os.environ, GAGP_CUDA_DEVICE=str(device)))
                write_json(directory / f"{variant}.process.json", {"command": command,
                    "returncode": result.returncode, "stdout": result.stdout, "stderr": result.stderr})
                result.check_returncode()
                observed[variant] = json.loads(path.read_text())
                commands[variant], hashes[variant] = command, sha256(path)
            if any(observed["plain"][key] != observed["instrumented"][key]
                   for key in ("best_programs", "fitness_history")):
                raise ValueError("population observer changed best programs or fitness history")
            populations = observed["instrumented"]["populations"]
            phases = ["evaluation", "reproduction"] * generations
            if options.get("--skip-final-eval", "off") != "on":
                phases.append("evaluation")
            if [p["phase"] for p in populations] != phases:
                raise ValueError("missing population observation")
            for population in populations:
                if host_probes and population["phase"] == "reproduction":
                    validate_host_decisions(population, mode_flags.get("--repro-backend") == "gpu")
                    if gpu_probes and mode_flags.get("--repro-backend") == "gpu":
                        validate_gpu_decisions(population)
                if (population["population_size"] != size or
                        any(sum(count for _, count in population[key]) != size for key in ("node_counts", "depths")) or
                        sum(population["post_backend_verifier_counts"].values()) != size):
                    raise ValueError("population histogram is incomplete")
                if population["phase"] == "evaluation" and (
                        population["fitness_evaluations_requested"] != size or population["case_score_requests"] != size * case_count):
                    raise ValueError("fitness evaluation accounting differs from frozen workload")
            manifest["captures"].append({"workload": workload["id"], "mode": mode,
                "workload_sha256": canonical_hash(workload), "commands": commands, "hashes": hashes,
                "history_and_best_programs_match": True, "population_observations": len(populations)})
            write_json(output / "manifest.json", manifest)
            print(workload["id"], mode, "captured", flush=True)
    manifest["status"] = ("complete distributions, host decisions and GPU kernel decision capture"
                          if gpu_probes else
                          "complete distributions and host decisions; GPU kernel decisions remain separate"
                          if host_probes else
                          "complete distribution capture; acceptance/fallback decisions remain separate")
    write_json(output / "manifest.json", manifest)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workloads", type=Path, required=True)
    parser.add_argument("--adapter-manifest", type=Path, required=True)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--device", type=int, required=True)
    args = parser.parse_args()
    capture(args.workloads.resolve(), args.adapter_manifest.resolve(), args.root.resolve(),
            args.output.resolve(), args.device)


if __name__ == "__main__":
    main()
