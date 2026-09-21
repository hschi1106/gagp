"""Materialize candidate compiled-grammar workloads before performance tuning."""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from typing import Any


VERSION = "compiled-migration-workloads-v1"
MODES = ("cpu", "gpu_eval", "gpu_repro", "gpu_repro_overlap", "cpu_eval_gpu_repro")
SIZES = (64, 1024)
GENERATIONS = (1, 20)
BLOCKS = (256, 1024)
TYPED_FUEL = 100

DOMAINS: tuple[tuple[str, str, list[Any]], ...] = (
    ("int", "Int", ["-3", "8"]),
    ("float", "Float", [-1.25, 2.5]),
    ("bool", "Bool", [False, True]),
    ("char", "Char", ["a", "z"]),
    ("string", "String", ["alpha", "beta"]),
    ("int_list", "IntList", [["1", "-2"], ["7"]]),
    ("float_list", "FloatList", [[1.25, -2.5], [7.5]]),
    ("string_list", "StringList", [["alpha", "beta"], ["gamma"]]),
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _write(path: Path, value: Any) -> dict[str, str]:
    path.write_text(json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n")
    return {"path": path.name, "sha256": sha256(path)}


def _typed_grammar(type_name: str, values: list[Any]) -> dict[str, Any]:
    return {
        "format_version": "grammar-definition-v1",
        "entry": {"nonterminal": "Main", "type": type_name},
        "search_limits": {"max_nodes": 12, "max_depth": 8},
        "execution_limits": {"fuel": TYPED_FUEL},
        "nonterminals": [{
            "id": "Main", "type": type_name, "scope": [],
            "alternatives": [{
                "id": "captured", "weight": 1,
                "expression": {
                    "signature": f"let({type_name},{type_name})->{type_name}",
                    "args": [
                        {"constant": {"type": type_name, "values": values}},
                        {"bound": "x"},
                    ],
                    "bind": {"1": ["x"]},
                },
            }],
        }],
    }


def _public_value(domain: str, value: Any) -> dict[str, Any]:
    if domain == "int":
        value = int(value)
    elif domain == "int_list":
        value = [int(item) for item in value]
    return {"type": domain, "value": value}


def _cases(inputs: dict[str, dict[str, Any]], expected: dict[str, Any]) -> dict[str, Any]:
    row = {"inputs": inputs, "expected": expected}
    return {"format_version": "fitness-cases", "cases": [row for _ in range(1024)]}


def _memo_expected(row: int, column: int, rows: int, columns: int, base: int) -> int:
    """Evaluate the checked-in memo recurrence without using the runtime."""
    values = [[0] * columns for _ in range(rows)]
    for current_row in range(rows):
        for current_column in range(columns):
            if current_row == 0:
                values[current_row][current_column] = base
                continue
            diagonal = (values[current_row - 1][current_column + 1]
                        if current_column + 1 < columns else 0)
            previous = values[current_row][current_column - 1] if current_column else 0
            values[current_row][current_column] = diagonal + previous
    return values[row][column]


def _artifact(path: Path, root: Path) -> dict[str, str]:
    return {"path": str(path.relative_to(root)), "sha256": sha256(path)}


def freeze(repository: Path, generator: Path, output: Path, *, seed: int = 42) -> dict[str, Any]:
    """Create a bounded candidate workload manifest and all of its input artifacts."""
    repository = repository.resolve()
    generator = generator.resolve()
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    if not generator.is_file():
        raise ValueError(f"generator does not exist: {generator}")

    profiles: list[dict[str, Any]] = []
    definitions: list[tuple[str, str, dict[str, Any], dict[str, Any], dict[str, Any]]] = []
    for domain, type_name, values in DOMAINS:
        name = f"typed_capture_{domain}"
        definitions.append((name, domain, _typed_grammar(type_name, values),
                            _cases({}, _public_value(domain, values[0])),
                            {"kind": "typed_lexical_capture"}))

    for name, expected, inputs, extra in (
        ("bounded_sequence", {"type": "string_list", "value": ["alpha", "beta", "gamma", "delta"]},
         {"source": {"type": "string_list", "value": ["alpha", "beta", "gamma", "delta"]}},
         {"kind": "bounded_sequence"}),
        ("bounded_memo", {"type": "int", "value": _memo_expected(7, 7, 8, 8, 3)},
         {"row": {"type": "int", "value": 7}, "column": {"type": "int", "value": 7},
          "rows": {"type": "int", "value": 8}, "columns": {"type": "int", "value": 8},
          "base_value": {"type": "int", "value": 3}},
         {"kind": "bounded_memo", "memo_cells": 128}),
    ):
        source = repository / "configs" / "grammar_definitions" / f"{name}.json"
        if not source.is_file():
            raise ValueError(f"source grammar does not exist: {source}")
        grammar = json.loads(source.read_text())
        if name == "bounded_memo":
            grammar["nonterminals"][0]["alternatives"][0]["expression"]["structured"]["plan"]["limits"]["cells"] = 128
        source_record = {"path": str(source.relative_to(repository)), "sha256": sha256(source)}
        domain = "string_list" if name == "bounded_sequence" else "int"
        definitions.append((name, domain, grammar, _cases(inputs, expected),
                            {**extra, "source_grammar": source_record}))

    generation_runs: list[dict[str, Any]] = []
    workloads: list[dict[str, Any]] = []
    for name, domain, grammar_json, cases_json, profile_extra in definitions:
        grammar = _write(output / f"{name}.grammar.json", grammar_json)
        cases = _write(output / f"{name}.cases.json", cases_json)
        fuel = grammar_json["execution_limits"]["fuel"]
        profile = {"id": name, "domain": domain, "fuel": fuel,
                   "grammar": grammar, "cases": cases, **profile_extra}
        profiles.append(profile)
        for size in SIZES:
            population_path = output / f"{name}-p{size}.population.json"
            command = ["rtk", "proxy", str(generator), "--grammar-definition",
                       str(output / grammar["path"]), "--cases", str(output / cases["path"]),
                       "--population-size", str(size), "--seed", str(seed),
                       "--out-json", str(population_path)]
            result = subprocess.run(command, cwd=repository, capture_output=True, text=True)
            run = {"profile": name, "population_size": size, "command": command,
                   "returncode": result.returncode, "stdout": result.stdout, "stderr": result.stderr}
            if result.returncode != 0:
                _write(output / f"{name}-p{size}.generation.json", run)
                raise RuntimeError(f"population generation failed for {name} p{size}: {result.stderr.strip()}")
            if not population_path.is_file():
                run["output_missing"] = True
                _write(output / f"{name}-p{size}.generation.json", run)
                raise RuntimeError(f"population generator did not create {population_path}")
            snapshot = _artifact(population_path, output)
            run["output"] = snapshot
            run["record"] = _write(output / f"{name}-p{size}.generation.json", run)
            generation_runs.append(run)
            for generations in GENERATIONS:
                for block in BLOCKS:
                    args = ["--population-size", str(size), "--generations", str(generations),
                            "--blocksize", str(block), "--seed", str(seed), "--fuel", str(fuel),
                            "--penalty", "1", "--skip-final-eval", "on",
                            "--retain-final-population", "off"]
                    workloads.append({
                        "id": f"{name}-p{size}-g{generations}-b{block}",
                        "profile": name, "domain": domain, "grammar": grammar,
                        "cases": cases, "snapshot": snapshot, "args": args,
                    })

    manifest = {
        "version": VERSION,
        "status": "candidate preregistration; measurements pending",
        "warmup_blocks": 3,
        "measured_blocks": 15,
        "analysis_seed": seed,
        "modes": list(MODES),
        "generator": {"path": str(generator), "sha256": sha256(generator)},
        "profiles": profiles,
        "generation_runs": generation_runs,
        "workloads": workloads,
    }
    _write(output / "workloads.json", manifest)
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, required=True)
    parser.add_argument("--generator", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()
    manifest = freeze(args.repository, args.generator, args.output, seed=args.seed)
    print(f"materialized {len(manifest['workloads'])} candidate workload configurations")


if __name__ == "__main__":
    main()
