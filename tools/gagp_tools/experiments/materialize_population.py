"""Materialize a compiled-grammar population for repeatable benchmark input."""
from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path
from typing import List


def build_command(args: argparse.Namespace) -> List[str]:
    return [
        str(args.generator),
        "--grammar-definition",
        str(args.grammar_definition),
        "--cases",
        str(args.cases),
        "--population-size",
        str(args.population_size),
        "--seed",
        str(args.seed),
        "--out-json",
        str(args.out),
    ]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Materialize a grammar-population-v2 benchmark input."
    )
    parser.add_argument("--generator", type=Path, default=Path("cpp/build/gagp_generate_cli"))
    parser.add_argument("--grammar-definition", required=True, type=Path)
    parser.add_argument("--cases", required=True, type=Path)
    parser.add_argument("--population-size", required=True, type=int)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()

    if args.population_size <= 0:
        parser.error("--population-size must be > 0")
    if args.seed < 0:
        parser.error("--seed must be >= 0")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(build_command(args), text=True, capture_output=True)
    if result.returncode != 0:
        raise SystemExit(result.stderr.strip() or f"population generation failed with exit {result.returncode}")
    if not args.out.is_file():
        raise SystemExit(f"population generator did not create {args.out}")
    try:
        artifact = json.loads(args.out.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"population generator wrote invalid JSON: {exc}") from exc
    if not isinstance(artifact, dict) or artifact.get("format_version") != "grammar-population-v2":
        raise SystemExit("population generator did not write grammar-population-v2")
    print(f"POPULATION_OUT {args.out}")
    print(f"POPULATION_SIZE {args.population_size}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
