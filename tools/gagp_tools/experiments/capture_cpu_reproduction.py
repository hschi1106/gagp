"""Capture CPU mutation donors and verify interception against original operators."""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
from pathlib import Path

from gagp_tools.experiments.grammar_migration import sha256, write_json


def capture(reference: Path, reference_build: Path, adapter_build: Path,
            repository: Path, captures: Path, output: Path) -> None:
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=reference, text=True).strip()
    if subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=no"], cwd=reference, text=True):
        raise ValueError("reference source has tracked changes")
    output.mkdir(parents=True, exist_ok=False)
    sources = ["cpp/src/bench/capture_cpu_donors.cpp", "cpp/src/bench/migration_snapshot.cpp",
               "cpp/src/bench/migration_snapshot.hpp", "cpp/src/bench/migration_reproduction.hpp"]
    for relative in [*sources, str(Path(__file__).resolve().relative_to(repository))]:
        destination = output / "source" / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(repository / relative, destination)
    libraries = [adapter_build / "libgagp_cli_support.a", *[reference_build / name for name in
        ("libgagp_evolution.a", "libgagp_runtime_cpu.a", "libgagp_core.a", "libgagp_gpu.a")]]
    mutation = reference / "cpp/src/evolution/mutation.cpp"
    manifest = {"version": "migration-cpu-reproduction-capture-v1", "reference_revision": revision,
        "parent_capture_manifest": str(captures), "parent_capture_manifest_sha256": sha256(captures),
        "mutation_source_sha256": sha256(mutation),
        "sources": {s: sha256(output / "source" / s) for s in sources},
        "libraries": {str(p): sha256(p) for p in libraries}, "builds": {}, "captures": []}
    for mode in ("plain", "instrumented"):
        command = ["c++", "-std=c++17", "-O3", "-DNDEBUG", "-DGAGP_HAS_CUDA=1",
            "-I", str(reference / "cpp/include"), "-I", str(reference / "cpp/src/evolution"),
            "-I", str(output / "source/cpp/src/bench"), str(output / "source" / sources[0]),
            str(output / "source" / sources[1]), "-o", str(output / mode), "-Wl,--start-group",
            *map(str, libraries), "-Wl,--end-group", "-L/usr/local/cuda/targets/x86_64-linux/lib",
            "-lcudadevrt", "-lcudart_static", "-lrt", "-lpthread", "-ldl"]
        if mode == "instrumented":
            command.insert(1, f'-DGAGP_MUTATION_SOURCE="{mutation}"')
        manifest["builds"][mode] = {"command": command}
        write_json(output / "manifest.json", manifest)
        result = subprocess.run(command, capture_output=True, text=True)
        (output / f"{mode}.build.log").write_text(result.stdout + result.stderr)
        result.check_returncode()
        manifest["builds"][mode]["sha256"] = sha256(output / mode)
    for row in json.loads(captures.read_text())["captures"]:
        if sha256(Path(row["path"])) != row["sha256"]:
            raise ValueError("reproduction parent capture changed")
        outputs, commands, hashes = {}, [], {}
        for mode in ("plain", "instrumented", "replay", "steady"):
            command = list(row["command"])
            command[0] = str(output / ("instrumented" if mode in ("replay", "steady") else mode))
            index = command.index("--action")
            del command[index:index + 2]
            command[command.index("--snapshot") + 1] = row["path"]
            path = output / f"{row['workload']}.{mode}.json"
            command[command.index("--out-json") + 1] = str(path)
            if mode in ("replay", "steady"):
                command.extend(["--donor-tape", str(output / f"{row['workload']}.instrumented.json")])
            if mode == "steady":
                command.append("--steady")
            if sha256(Path(command[command.index("--cases") + 1])) != row["cases_sha256"]:
                raise ValueError("reproduction case fixture changed")
            result = subprocess.run(command, capture_output=True, text=True)
            write_json(output / f"{row['workload']}.{mode}.process.json", {
                "command": command, "returncode": result.returncode, "stdout": result.stdout, "stderr": result.stderr})
            result.check_returncode()
            outputs[mode] = json.loads(path.read_text())
            hashes[mode] = sha256(path)
            commands.append(command)
        if any(outputs["plain"][key] != outputs[mode][key]
               for mode in ("instrumented", "replay", "steady") for key in ("children", "verification")):
            raise ValueError("donor interception changed production children")
        if (outputs["replay"]["generated_calls"] != 0 or
                outputs["replay"]["replayed_calls"] != len(outputs["instrumented"]["calls"])):
            raise ValueError("donor replay did not consume the exact frozen tape")
        if outputs["steady"]["generated_calls"] != 0 or len(outputs["steady"]["samples"]) != 18:
            raise ValueError("steady replay did not measure the frozen tape")
        manifest["captures"].append({"workload": row["workload"], "commands": commands,
            "children_match": True, "donor_calls": len(outputs["instrumented"]["calls"]),
            "replay_match": True, "replay_generated_calls": outputs["replay"]["generated_calls"],
            "steady_calls": len(outputs["steady"]["samples"]),
            "rejected_children": sum(not v["ok"] for v in outputs["plain"]["verification"]), "hashes": hashes})
        write_json(output / "manifest.json", manifest)
        print(row["workload"], "captured", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("reference", "reference-build", "adapter-build", "captures", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    args = parser.parse_args()
    capture(args.reference.resolve(), args.reference_build.resolve(), args.adapter_build.resolve(),
            args.repository.resolve(), args.captures.resolve(), args.output.resolve())


if __name__ == "__main__":
    main()
