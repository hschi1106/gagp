"""Check migration trial completeness against independently frozen workloads."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from gagp_tools.experiments.grammar_migration import (
    canonical_hash, extract_scopes, measurement_command, sha256, validate_manifest,
    workload_modes, workload_scopes,
)


def audit_trials(directory: Path, manifest: dict[str, Any], root: Path) -> dict[str, Any]:
    validate_manifest(manifest, root)
    identity = json.loads((directory / "identity.json").read_text())
    report = json.loads((directory / "trials.json").read_text())
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
    if any(w.get("measurement") == "steady_repro" for w in manifest["workloads"]) and set(cpu_binaries) != set(roles):
        raise ValueError("missing CPU reproduction executable identity")
    for binary in cpu_binaries.values():
        if sha256(Path(binary["path"])) != binary["sha256"]:
            raise ValueError("CPU reproduction executable hash changed")

    required = {f"{w['id']}/{mode}/{scope}": w for w in manifest["workloads"]
                for mode in workload_modes(w, manifest["modes"]) for scope in workload_scopes(w)}
    donor_counts = {w["id"]: len(json.loads((root / w["donor_tape"]["path"]).read_text())["calls"])
                    for w in manifest["workloads"] if w.get("measurement") == "steady_repro"}
    if (set(report["rows"]) != set(required) or set(report["required_rows"]) != set(required)
            or len(report["required_rows"]) != len(required)):
        raise ValueError("trial rows do not cover the independently frozen matrix")
    count = manifest["warmup_blocks"] + manifest["measured_blocks"]
    observations = {}
    for name, workload in required.items():
        row = report["rows"][name]
        if any(row[key] != canonical_hash(workload)
               for key in ("workload_before_sha256", "workload_after_sha256")):
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
    for block in range(count):
        for index, workload in enumerate(manifest["workloads"]):
            modes = workload_modes(workload, manifest["modes"])
            folder = directory / f"block-{block:03d}" / f"workload-{index:03d}"
            names = [f"{workload['id']}/{m}/{s}" for m in modes for s in workload_scopes(workload)]
            if not all(block in observations[name] for name in names):
                missing.append({"block": block, "workload": workload["id"]})
                continue
            order = json.loads((folder / "order.json").read_text())
            expected_order = {(role, mode) for role in roles for mode in modes}
            if len(order) != len(expected_order) or {tuple(entry) for entry in order} != expected_order:
                raise ValueError("raw execution order is incomplete or duplicated")
            values = {}
            for role, mode in order:
                path = folder / f"{role}-{mode}.json"
                process = json.loads((folder / f"{role}-{mode}.process.json").read_text())
                cpu_binary = Path(cpu_binaries[role]["path"]) if role in cpu_binaries else None
                expected = measurement_command(workload, mode, Path(identity["binaries"][role]["path"]),
                                               root, path, cpu_binary)
                if process["returncode"] != 0 or process["command"] != expected:
                    raise ValueError("raw process failed or command differs from frozen workload")
                payload = json.loads(path.read_text())
                if workload.get("measurement") == "steady_repro":
                    expected_format = ("migration-steady-cpu-reproduction-v1" if mode == "cpu"
                                       else "migration-steady-reproduction-v1")
                    if payload.get("format_version") != expected_format:
                        raise ValueError("reproduction output came from the wrong measurement path")
                    if mode == "cpu" and (payload["generated_calls"] != 0 or
                                          payload["replayed_calls"] != donor_counts[workload["id"]]):
                        raise ValueError("CPU reproduction did not consume the frozen donors exactly")
                if workload.get("measurement") in ("steady_eval", "steady_repro"):
                    if (payload["warmups"] != workload["session_warmups"]
                            or payload["measured_trials"] != workload["session_trials"]):
                        raise ValueError("raw session repetition count differs from frozen protocol")
                else:
                    options = dict(zip(workload["args"][::2], workload["args"][1::2]))
                    if len(payload["generations"]) != int(options["--generations"]):
                        raise ValueError("raw generation count differs from frozen workload")
                values[role, mode] = extract_scopes(payload, process["wall_ms"])
                checked += 1
            for mode in modes:
                for scope in workload_scopes(workload):
                    observation = observations[f"{workload['id']}/{mode}/{scope}"][block]
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
