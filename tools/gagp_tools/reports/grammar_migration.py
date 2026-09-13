"""Paired block statistics for the frozen grammar migration performance gate."""
from __future__ import annotations

import argparse
import json
import math
import random
import statistics
from pathlib import Path
from typing import Any

from gagp_tools.shared.metrics import interpolated_percentile


def compare_row(blocks: list[dict[str, Any]], *, seed: int,
                resamples: int = 10_000) -> dict[str, Any]:
    """Resample complete CPU/mode before/after blocks, preserving pairing.

    Caller supplies one frozen workload/mode/scope row. Warm-ups and exclusions
    are retained in the input but never silently treated as measured blocks.
    """
    if resamples < 10_000:
        raise ValueError("at least 10000 bootstrap resamples required")
    keys = ("cpu_before_ms", "mode_before_ms", "cpu_after_ms", "mode_after_ms")
    measured = []
    seen = set()
    warmups = 0
    exclusions = []
    for block in blocks:
        identity = block["block_id"]
        if identity in seen:
            raise ValueError("duplicate block_id")
        seen.add(identity)
        if block.get("excluded"):
            if not isinstance(block.get("reason"), str) or not block["reason"].strip():
                raise ValueError("excluded blocks require a reason")
            exclusions.append({"block_id": identity, "reason": block["reason"]})
            continue
        values = [block[key] for key in keys]
        if any(isinstance(v, bool) or not isinstance(v, (int, float))
               or not math.isfinite(v) or v <= 0 for v in values):
            raise ValueError("timings must be finite positive milliseconds")
        if block.get("warmup", False):
            warmups += 1
        else:
            measured.append(values)
    n = len(measured)
    if warmups < 3 or n < 15:
        return {"status": "pending", "reason": "need 3 warmups and 15 measured blocks",
                "samples": n, "warmups": warmups, "exclusions": exclusions}

    def ratios(sample: list[list[float]]) -> tuple[float, float, float]:
        cb, mb, ca, ma = (statistics.median(col) for col in zip(*sample))
        return (ca / ma) / (cb / mb), mb / ma, cb / ca

    estimates = ratios(measured)
    medians = dict(zip(keys, (statistics.median(col) for col in zip(*measured))))
    rng = random.Random(seed)
    draws = [ratios([measured[rng.randrange(n)] for _ in range(n)])
             for _ in range(resamples)]
    metrics = {}
    for i, name in enumerate(("Q", "A_mode", "A_cpu")):
        distribution = [draw[i] for draw in draws]
        metrics[name] = {"estimate": estimates[i], "ci95": [
            interpolated_percentile(distribution, 0.025),
            interpolated_percentile(distribution, 0.975)]}
    passing = all(m["estimate"] >= 1.0 and m["ci95"][0] >= 0.97
                  for m in metrics.values())
    status = "pass" if passing else ("rerun" if n < 30 else "pending")
    if n >= 30 and any(m["ci95"][1] < 1.0 for m in metrics.values()):
        status = "regression"
    return {"status": status, "samples": n, "warmups": warmups,
            "seed": seed, "resamples": resamples, "metrics": metrics,
            "median_ms": medians,
            "speedup_before": medians["cpu_before_ms"] / medians["mode_before_ms"],
            "speedup_after": medians["cpu_after_ms"] / medians["mode_after_ms"],
            "exclusions": exclusions}


def compare_manifest(manifest: dict[str, Any]) -> dict[str, Any]:
    if manifest.get("version") != "grammar-migration-trials-v1":
        raise ValueError("unsupported trial manifest version")
    required = manifest["required_rows"]
    if not required or len(required) != len(set(required)):
        raise ValueError("required_rows must be nonempty and unique")
    rows = manifest["rows"]
    if set(rows) != set(required):
        raise ValueError("missing or unexpected workload/mode/scope rows")
    results = {}
    for name in required:
        row = rows[name]
        if not row["workload_before_sha256"] or row["workload_before_sha256"] != row["workload_after_sha256"]:
            raise ValueError(f"changed frozen workload: {name}")
        canonical_cold = (row.get("scope") == "canonical_cold" and
                          row["timing_source"] == "canonical_cold_disjoint")
        if row["timing_source"] != "direct" and not canonical_cold:
            raise ValueError("timings must be direct measurements, not summed overlapping phases")
        results[name] = compare_row(row["blocks"], seed=manifest["analysis_seed"])
    return {"version": "grammar-migration-comparison-v1",
            "status": "pass" if all(r["status"] == "pass" for r in results.values()) else "pending",
            "rows": results}


def main() -> None:
    from gagp_tools.reports.audit_migration_trials import audit_trials

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--workloads", required=True, type=Path,
                        help="independently frozen workload manifest")
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    directory = args.manifest.resolve().parent
    if args.manifest.name != "trials.json":
        parser.error("comparison requires the runner's trials.json and sibling raw evidence")
    if args.output.resolve().is_relative_to(directory):
        parser.error("comparison output must be outside the immutable trial directory")
    audit = audit_trials(directory, json.loads(args.workloads.read_text()), args.root.resolve())
    result = compare_manifest(json.loads(args.manifest.read_text()))
    result["evidence_audit"] = audit
    if audit["status"] != "complete":
        result["status"] = "pending"
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    if result["status"] != "pass":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
