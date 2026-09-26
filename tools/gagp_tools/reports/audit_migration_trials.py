"""Check migration trial completeness against independently frozen workloads."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any

from gagp_tools.experiments.grammar_migration import (
    canonical_hash, extract_scopes, measurement_command, sha256, validate_manifest,
    uses_cpu_donor_replay, workload_for_role, workload_hash, workload_id,
    workload_modes, workload_scopes,
)


def check_fitness_agreement(vectors: dict[tuple[str, str], list[float]],
                           roles: tuple[str, ...], modes: list[str] | tuple[str, ...]) -> float:
    """Preserve each backend exactly; report CPU/GPU bounded-fallback differences."""
    maximum_difference = 0.0
    for mode in modes:
        reference = vectors[roles[0], mode]
        for role in roles:
            if vectors[role, mode] != reference:
                raise ValueError("raw fitness differs across binary roles for the same engine")
            cpu = vectors[role, "cpu"]
            if len(cpu) != len(reference):
                raise ValueError("raw fitness population lengths differ across engines")
            difference = max((abs(a - b) for a, b in zip(cpu, reference)), default=0.0)
            maximum_difference = max(maximum_difference, difference)
    return maximum_difference


def audit_trials(directory: Path, manifest: dict[str, Any], root: Path) -> dict[str, Any]:
    validate_manifest(manifest, root)
    identity = json.loads((directory / "identity.json").read_text())
    report = json.loads((directory / "trials.json").read_text())
    if report.get("acceptance_protocol", "frozen-v1") != manifest.get("acceptance_protocol", "frozen-v1"):
        raise ValueError("trial acceptance protocol differs from workload manifest")
    if identity["manifest_sha256"] != canonical_hash(manifest) or identity["manifest"] != manifest:
        raise ValueError("trial identity differs from frozen workload manifest")
    if report["analysis_seed"] != manifest["analysis_seed"]:
        raise ValueError("trial analysis seed differs from frozen protocol")
    paired = report["version"] == "grammar-migration-trials-v1"
    if not paired and report["version"] != "grammar-migration-baseline-trials-v1":
        raise ValueError("unsupported trial format")
    roles = ("before", "after") if paired else ("before",)
    if set(identity["binaries"]) != set(roles):
        raise ValueError("trial binary roles disagree with report format")
    for binary in identity["binaries"].values():
        if sha256(Path(binary["path"])) != binary["sha256"]:
            raise ValueError("measurement binary hash changed")
    cpu_binaries = identity.get("cpu_reproduction_binaries", {})
    replay_roles = {role for role in roles for workload in manifest["workloads"]
                    if uses_cpu_donor_replay(workload, role)}
    if set(cpu_binaries) != replay_roles:
        raise ValueError("CPU donor replay executable identity differs from the frozen protocol")
    for binary in cpu_binaries.values():
        if sha256(Path(binary["path"])) != binary["sha256"]:
            raise ValueError("CPU reproduction executable hash changed")

    required = {f"{workload_id(w)}/{mode}/{scope}": w for w in manifest["workloads"]
                for mode in workload_modes(w, manifest["modes"]) for scope in workload_scopes(w)}
    donor_counts = {(workload_id(w), role): len(json.loads((root / workload_for_role(w, role)["donor_tape"]["path"]).read_text())["calls"])
                    for w in manifest["workloads"] if w.get("measurement") == "steady_repro"
                    for role in roles if uses_cpu_donor_replay(w, role)}
    if (set(report["rows"]) != set(required) or set(report["required_rows"]) != set(required)
            or len(report["required_rows"]) != len(required)):
        raise ValueError("trial rows do not cover the independently frozen matrix")
    count = manifest["warmup_blocks"] + manifest["measured_blocks"]
    observations = {}
    for name, workload in required.items():
        row = report["rows"][name]
        if (row["workload_before_sha256"] != workload_hash(workload, "before")
                or row["workload_after_sha256"] != workload_hash(workload, "after")):
            raise ValueError("row workload hash differs from frozen workload")
        scope = name.rsplit("/", 1)[1]
        source = "canonical_cold_disjoint" if scope == "canonical_cold" else "direct"
        if row["scope"] != scope or row["timing_source"] != source:
            raise ValueError("row scope or timing source changed")
        indexed = {}
        for observation in row["blocks"]:
            block = observation["block_id"]
            if type(block) is not int or block not in range(count) or block in indexed:
                raise ValueError("unexpected or duplicate trial block")
            if observation.get("warmup") is not (block < manifest["warmup_blocks"]):
                raise ValueError("trial warmup classification changed")
            if observation.get("excluded"):
                raise ValueError("exclusions require a separately frozen replacement protocol")
            indexed[block] = observation
        observations[name] = indexed

    missing = []
    checked = 0
    checked_fitness_vectors = 0
    maximum_fitness_difference = 0.0
    for block in range(count):
        for index, workload in enumerate(manifest["workloads"]):
            modes = workload_modes(workload, manifest["modes"])
            folder = directory / f"block-{block:03d}" / f"workload-{index:03d}"
            names = [f"{workload_id(workload)}/{m}/{s}" for m in modes for s in workload_scopes(workload)]
            if not all(block in observations[name] for name in names):
                missing.append({"block": block, "workload": workload_id(workload)})
                continue
            order = json.loads((folder / "order.json").read_text())
            expected_order = {(role, mode) for role in roles for mode in modes}
            if len(order) != len(expected_order) or {tuple(entry) for entry in order} != expected_order:
                raise ValueError("raw execution order is incomplete or duplicated")
            values = {}
            fitness_vectors = {}
            for role, mode in order:
                path = folder / f"{role}-{mode}.json"
                process = json.loads((folder / f"{role}-{mode}.process.json").read_text())
                cpu_binary = Path(cpu_binaries[role]["path"]) if role in cpu_binaries else None
                expected = measurement_command(workload, mode, Path(identity["binaries"][role]["path"]),
                                               root, path, cpu_binary, role)
                if process["returncode"] != 0 or process["command"] != expected:
                    raise ValueError("raw process failed or command differs from frozen workload")
                payload = json.loads(path.read_text())
                if workload.get("measurement") == "steady_eval":
                    side = workload_for_role(workload, role)
                    options = dict(zip(side["args"][::2], side["args"][1::2]))
                    fitness = payload.get("fitness")
                    if (payload.get("format_version") != "migration-steady-eval-v1"
                            or payload.get("engine") != ("cpu" if mode == "cpu" else "gpu")):
                        raise ValueError("evaluation output came from the wrong measurement path")
                    if (not isinstance(fitness, list)
                            or len(fitness) != int(options["--population-size"])
                            or any(type(value) not in (int, float) or not math.isfinite(value)
                                   for value in fitness)):
                        raise ValueError("raw fitness does not cover the frozen population")
                    fitness_vectors[role, mode] = fitness
                    checked_fitness_vectors += 1
                if workload.get("measurement") == "steady_repro":
                    expected_format = ("migration-steady-cpu-reproduction-v1" if mode == "cpu"
                                       else "migration-steady-reproduction-v1")
                    if payload.get("format_version") != expected_format:
                        raise ValueError("reproduction output came from the wrong measurement path")
                    if mode == "cpu" and uses_cpu_donor_replay(workload, role) and (
                                          payload["generated_calls"] != 0 or
                                          payload["replayed_calls"] != donor_counts[workload_id(workload), role]):
                        raise ValueError("CPU reproduction did not consume the frozen donors exactly")
                if workload.get("measurement") in ("steady_eval", "steady_repro"):
                    if (payload["warmups"] != workload["session_warmups"]
                            or payload["measured_trials"] != workload["session_trials"]):
                        raise ValueError("raw session repetition count differs from frozen protocol")
                else:
                    side = workload_for_role(workload, role)
                    options = dict(zip(side["args"][::2], side["args"][1::2]))
                    if len(payload["generations"]) != int(options["--generations"]):
                        raise ValueError("raw generation count differs from frozen workload")
                values[role, mode] = extract_scopes(payload, process["wall_ms"],
                    manifest.get("acceptance_protocol", "frozen-v1"))
                checked += 1
            if fitness_vectors:
                maximum_fitness_difference = max(maximum_fitness_difference,
                    check_fitness_agreement(fitness_vectors, roles, modes))
            for mode in modes:
                for scope in workload_scopes(workload):
                    observation = observations[f"{workload_id(workload)}/{mode}/{scope}"][block]
                    for role in roles:
                        if (observation[f"cpu_{role}_ms"] != values[role, "cpu"][scope]
                                or observation[f"mode_{role}_ms"] != values[role, mode][scope]):
                            raise ValueError("reported timing differs from raw measurements")
    hashes = directory / "raw-sha256.json"
    replacements = []
    for replacement in sorted((directory / "failed-attempts").glob("**/replacement.json")):
        record = json.loads(replacement.read_text())
        actual = {str(p.relative_to(replacement.parent)): sha256(p)
                  for p in replacement.parent.rglob("*.json") if p != replacement}
        if record["files_sha256"] != actual:
            raise ValueError("archived incomplete workload evidence changed")
        processes = [json.loads(p.read_text()) for p in replacement.parent.glob("*.process.json")]
        reason = "failed_process" if any(p["returncode"] for p in processes) else "interrupted_uncommitted_workload_block"
        if record["reason"] != reason:
            raise ValueError("replacement reason does not match archived process evidence")
        replacements.append({"path": str(replacement.relative_to(directory)), "reason": reason})
    if hashes.exists():
        recorded = json.loads(hashes.read_text())
        actual = {str(p.relative_to(directory)): sha256(p) for p in directory.rglob("*.json")
                  if p != hashes}
        if recorded != actual:
            raise ValueError("raw evidence file inventory or hash changed")
    return {"version": "grammar-migration-audit-v1", "status": "complete" if not missing and hashes.exists() else "pending",
            "paired": paired, "required_rows": len(required), "checked_processes": checked,
            "checked_fitness_vectors": checked_fitness_vectors,
            "maximum_cpu_gpu_fitness_difference": maximum_fitness_difference,
            "fitness_contract": "exact-before-after-per-backend",
            "cpu_gpu_difference_is_diagnostic": True,
            "missing_blocks": missing, "raw_hash_inventory_present": hashes.exists(), "replacements": replacements}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--workloads", type=Path, required=True)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path, required=True,
                        help="write outside the immutable trial directory")
    args = parser.parse_args()
    if args.output.resolve().is_relative_to(args.directory.resolve()):
        parser.error("audit output must be outside the immutable trial directory")
    result = audit_trials(args.directory.resolve(), json.loads(args.workloads.read_text()), args.root.resolve())
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    if result["status"] != "complete":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
