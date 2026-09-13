"""Materialize the required migration workload families from reference artifacts."""
from __future__ import annotations

import argparse
import json
import math
import struct
import subprocess
from pathlib import Path
from typing import Any

from gagp_tools.experiments.grammar_migration import MODES, canonical_hash, sha256


def public_value(raw: dict[str, Any]) -> dict[str, Any]:
    tag = raw["tag"]
    bits = int(raw["bits"], 16)
    types = ("int", "float", "bool", "char", "string", "int_list", "float_list", "string_list")
    if tag >= len(types) or raw.get("materialized") is False:
        raise ValueError("opaque values require raw evaluation cases")
    if tag == 0:
        value = bits if bits < 2**63 else bits - 2**64
        if int(float(value)) != value:
            raise ValueError("public case codec cannot preserve this integer")
    elif tag == 1:
        value = struct.unpack(">d", bytes.fromhex(raw["bits"]))[0]
        if not math.isfinite(value):
            raise ValueError("public case codec cannot preserve non-finite floats")
    elif tag == 2:
        value = raw["bool"]
    elif tag == 3:
        if bits > 127:
            raise ValueError("use raw bindings for non-ASCII Char observations")
        value = chr(bits)
    elif tag == 4:
        value = bytes.fromhex(raw["bytes_hex"]).decode("utf-8")
    else:
        value = [public_value(element)["value"] for element in raw["elements"]]
    return {"type": types[tag], "value": value}


def freeze(reference: Path, oracle: Path, adapter: Path, output: Path,
           asgp_block1024: bool = False) -> dict[str, Any]:
    output.mkdir(parents=True, exist_ok=False)
    oracle_manifest = json.loads((oracle / "manifest.json").read_text())
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=reference, text=True).strip()
    if revision != oracle_manifest["reference_revision"]:
        raise ValueError("oracle and source revisions differ")
    if subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=no"], cwd=reference, text=True):
        raise ValueError("reference source has tracked changes")
    observations = {}
    executions = {}
    for capture in oracle_manifest["captures"]:
        path = oracle / capture["output"]
        if sha256(path) != capture["output_sha256"]:
            raise ValueError("oracle capture hash changed")
        for line in path.read_text().splitlines():
            row = json.loads(line)
            observations[capture["output"], row["ordinal"]] = row
            if row["kind"] == "execution" and not row["inputs"] and row["at_probe_cap"]["error"] is None:
                executions[canonical_hash(row["bytecode"])] = row

    def captured(file: int, ordinal: int) -> tuple[dict[str, Any], dict[str, Any]]:
        row = observations[f"capture-{file}.jsonl", ordinal]
        execution = executions[canonical_hash(row["bytecode"])]
        program = row["population"]["programs"][0]
        expected = public_value(execution["at_probe_cap"]["value"])
        return program, {"inputs": {}, "expected": expected}

    recipes = {
        "bool_char": [(2, 2883), (2, 2895)],
        "int_list_map": [(2, 3011)],
        "int_list_filter": [(0, 2)],
        "float_list_map": [(2, 3026)],
        "string_list_filter": [(2, 3028)],
        "string_list_empty_map": [(2, 3030)],
        "linear_int": [(2, 3013)],
        "linear_float": [(2, 3032)],
        "linear_string": [(2, 3034)],
        "dc_int": [(2, 3041)],
        "dc_float": [(2, 3042)],
        "nested_binders": [(0, 18)],
        "mixed_exact_payloads": [(2, 3011), (2, 3026), (2, 3028), (2, 3034)],
        "metadata_stress": [(0, 18), (2, 3013), (2, 3041), (2, 3042)],
    }
    families = {name: [captured(*origin) for origin in origins] for name, origins in recipes.items()}
    commands = []
    # Public DP fixtures are separate from the deliberately invalid phase-scope
    # ASTs in the parity capture. Freeze these through the reference initializer.
    for kind in ("dp1d", "dp2d"):
        path = output / f"{kind}-one.population.json"
        command = [str(adapter), "--action", "freeze", "--snapshot", str(path), "--source-ast",
                   str(reference / f"cpp/tests/fixtures/ast_eval_asgp_{kind}.json"), "--cases",
                   str(reference / "cpp/tests/fixtures/ast_eval_asgp_scalar_cases.json"), "--population-size", "1"]
        subprocess.run(command, check=True, capture_output=True, text=True)
        commands.append(command)
        families[kind] = [(json.loads(path.read_text())["programs"][0],
                          {"inputs": {}, "expected": {"type": "int", "value": 1}})]
        families["metadata_stress"].extend(families[kind])

    manifest = {"version": "migration-workloads-v1", "warmup_blocks": 3, "measured_blocks": 15,
                "analysis_seed": 20260911, "modes": list(MODES), "workloads": []}
    coverage = {"reference_revision": revision, "oracle_manifest_sha256": sha256(oracle / "manifest.json"),
                "adapter_sha256": sha256(adapter), "family_origins": recipes,
                "capability_exclusions": [], "commands": commands,
                "asgp_block1024": asgp_block1024,
                "status": "materialized; execution and full coverage audit pending"}
    coverage["mode_inventory"] = {
        "base_modes": ["cpu", "gpu_eval", "gpu_repro", "gpu_repro_overlap", "cpu_eval_gpu_repro"],
        "additional_modes": [name for name in MODES if name not in
                             ("cpu", "gpu_eval", "gpu_repro", "gpu_repro_overlap", "cpu_eval_gpu_repro")],
        "reason": "CLI exposes three GPU-assisted CPU reproduction ablations with either evaluator; included despite experiment-only documentation",
        "overlap": "only GPU evaluation plus GPU reproduction activates overlap; other flag combinations are aliases"}

    def write(name: str, payload: Any) -> dict[str, str]:
        path = output / name
        path.write_text(json.dumps(payload, separators=(",", ":"), allow_nan=False) + "\n")
        return {"path": str(path), "sha256": sha256(path)}

    def arguments(size: int, generations: int, block: int) -> list[str]:
        return ["--population-size", str(size), "--generations", str(generations), "--blocksize", str(block),
                "--seed", "42", "--fuel", "20000", "--penalty", "1", "--skip-final-eval", "on",
                "--retain-final-population", "off", "--max-expr-depth", "7", "--max-stmts-per-block", "6",
                "--max-total-nodes", "80", "--max-for-k", "16", "--max-call-args", "3"]

    def add_family(name: str, snapshot: dict[str, str], cases: dict[str, str], size: int, asgp: bool):
        blocks = (256,) if asgp and not asgp_block1024 else (256, 1024)
        if asgp and not asgp_block1024:
            coverage["capability_exclusions"].append({"family": name, "population_size": size, "blocksize": 1024,
                "reason": "reference ASGP kernel uses 142 registers; 1024-thread launch fails; 256 is the measured supported equivalent"})
        for block in blocks:
            for generations in (1, 20):
                manifest["workloads"].append({"id": f"{name}-p{size}-g{generations}-b{block}",
                    "measurement": "evolution", "snapshot": snapshot, "cases": cases,
                    "args": arguments(size, generations, block)})
            manifest["workloads"].append({"id": f"{name}-p{size}-steady-b{block}",
                "measurement": "steady_eval", "session_warmups": 3, "session_trials": 15,
                "snapshot": snapshot, "cases": cases, "args": arguments(size, 1, block)})

    for name, entries in families.items():
        case_rows = [entries[i % len(entries)][1] for i in range(1024)]
        cases = write(f"{name}.cases.json", {"format_version": "fitness-cases", "cases": case_rows})
        asgp = any(node["kind"] in (56, 57, 58) for entry in entries for node in entry[0]["structure"]["nodes"])
        for size in (64, 1024):
            snapshot = write(f"{name}-{size}.population.json", {"format_version": "migration-population-v1",
                "programs": [entries[i % len(entries)][0] for i in range(size)]})
            add_family(name, snapshot, cases, size, asgp)

    canonical = reference / "data/fixtures/simple_exp_1024.json"
    for size in (64, 1024):
        path = output / f"simple_exp-{size}.population.json"
        command = [str(adapter), "--action", "freeze", "--snapshot", str(path), "--cases", str(canonical),
                   "--population-size", str(size), "--seed", "42"]
        subprocess.run(command, check=True, capture_output=True, text=True)
        commands.append(command)
        add_family("simple_exp_1024", {"path": str(path), "sha256": sha256(path)},
                   {"path": str(canonical), "sha256": sha256(canonical)}, size, False)

    fallback = next(row for row in observations.values() if row["kind"] == "execution" and
                    row["result"].get("value", {}).get("tag") == 8 and not row["runtime_negative_test"])
    fallback_cases = write("bounded_fallback.cases.json", {"format_version": "migration-evaluation-cases-v1",
        "cases": [{"inputs": fallback["inputs"], "expected": fallback["expected"]} for _ in range(1024)]})
    for size in (64, 1024):
        snapshot = write(f"bounded_fallback-{size}.bytecode.json", {"format_version": "migration-bytecode-population-v1",
            "programs": [fallback["bytecode"] for _ in range(size)]})
        for block in (256, 1024):
            manifest["workloads"].append({"id": f"bounded_fallback-p{size}-steady-b{block}",
                "measurement": "steady_eval", "session_warmups": 3, "session_trials": 15,
                "snapshot": snapshot, "cases": fallback_cases, "args": arguments(size, 1, block)})
    coverage["bounded_fallback_origin"] = {"ordinal": fallback["ordinal"], "bytecode_sha256": canonical_hash(fallback["bytecode"]),
        "scope": "runtime microbenchmark; opaque input tokens cannot be regenerated from public fitness-case JSON"}
    coverage["remaining"] = ["all-row backend execution/fitness checks", "parent/donor workload snapshots",
        "generation size/NFE/acceptance distributions", "complete lexical/error-order/payload-overflow coverage audit",
        "statistical/exclusion policy audit and measured full baseline"]
    write("workloads.json", manifest)
    write("coverage.json", coverage)
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--oracle", type=Path, required=True)
    parser.add_argument("--adapter", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--asgp-block1024", action="store_true",
                        help="include ASGP 1024-thread rows after validating the reference build supports them")
    args = parser.parse_args()
    result = freeze(args.reference.resolve(), args.oracle.resolve(), args.adapter.resolve(), args.output.resolve(),
                    asgp_block1024=args.asgp_block1024)
    print(f"materialized {len(result['workloads'])} workload rows; validation remains required")


if __name__ == "__main__":
    main()
