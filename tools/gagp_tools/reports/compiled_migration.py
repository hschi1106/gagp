"""Summarize bounded candidate-only compiled-migration measurements."""
from __future__ import annotations

import argparse
import json
import math
import statistics
from pathlib import Path
from typing import Any

from gagp_tools.experiments.compiled_migration import (
    DIRECT_TIMING_SCOPES,
    EVALUATION_TIMINGS,
    MODES,
    REPRODUCTION_TIMINGS,
    VARIATION_COUNTERS,
    _max_rss,
    canonical_hash,
    extract_measurements,
    measurement_command,
    sha256,
    validate_manifest,
)
from gagp_tools.shared.metrics import interpolated_percentile


VERSION = "compiled-migration-candidate-summary-v1"
TRIAL_VERSION = "compiled-migration-candidate-trials-v1"
IDENTITY_VERSION = "compiled-migration-candidate-identity-v1"
EXPECTED_PHASES = {
    "generation_eval_ms", "generation_repro_ms", "generation_total_ms",
    *(f"generation_{key}" for key in EVALUATION_TIMINGS),
    *(f"generation_repro_{key}" for key in REPRODUCTION_TIMINGS),
}
EXPECTED_SCOPES = {"process_wall_ms", *DIRECT_TIMING_SCOPES}


def _load_object(path: Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid or missing {label}: {path}") from error
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be a JSON object")
    return value


def _number(value: Any, label: str) -> float:
    if (isinstance(value, bool) or not isinstance(value, (int, float)) or
            not math.isfinite(value) or value < 0):
        raise ValueError(f"{label} must be a finite non-negative number")
    return float(value)


def _integer(value: Any, label: str, *, positive: bool = False) -> int:
    if (isinstance(value, bool) or not isinstance(value, int) or value < (1 if positive else 0)):
        qualifier = "positive" if positive else "non-negative"
        raise ValueError(f"{label} must be a {qualifier} integer")
    return value


def _option_integer(workload: dict[str, Any], option: str) -> int:
    args = workload["args"]
    try:
        raw = args[args.index(option) + 1]
        value = int(raw)
    except (ValueError, IndexError) as error:
        raise ValueError(f"workload {workload['id']} has invalid {option}") from error
    if value <= 0 or str(value) != raw:
        raise ValueError(f"workload {workload['id']} has invalid {option}")
    return value


def _distribution(values: list[float]) -> dict[str, int | float]:
    if not values:
        raise ValueError("cannot summarize an empty distribution")
    return {
        "count": len(values),
        "min": min(values),
        "median": statistics.median(values),
        "mean": statistics.fmean(values),
        "p90": interpolated_percentile(values, 0.90),
        "max": max(values),
    }


def _validate_inventory(directory: Path) -> tuple[dict[str, str], Path]:
    inventory_path = directory / "raw-sha256.json"
    inventory = _load_object(inventory_path, "raw hash inventory")
    if not inventory:
        raise ValueError("raw hash inventory must not be empty")
    actual: dict[str, str] = {}
    for path in sorted(directory.rglob("*")):
        if path.is_file() and path != inventory_path:
            relative = str(path.relative_to(directory))
            actual[relative] = sha256(path)
    for relative, digest in inventory.items():
        if (not isinstance(relative, str) or not isinstance(digest, str) or
                len(digest) != 64 or any(character not in "0123456789abcdef" for character in digest)):
            raise ValueError("raw hash inventory has an invalid entry")
        candidate = Path(relative)
        if candidate.is_absolute() or ".." in candidate.parts:
            raise ValueError("raw hash inventory path escapes the trial directory")
    if inventory != actual:
        raise ValueError("raw evidence file inventory or hash changed")
    return inventory, inventory_path


def _validate_identity(directory: Path, manifest: dict[str, Any]) -> tuple[dict[str, Any], Path]:
    path = directory / "identity.json"
    identity = _load_object(path, "candidate identity")
    if identity.get("version") != IDENTITY_VERSION:
        raise ValueError("unsupported candidate identity version")
    if identity.get("manifest_sha256") != canonical_hash(manifest):
        raise ValueError("candidate identity does not match the frozen workload manifest")
    if identity.get("generator") != manifest.get("generator"):
        raise ValueError("candidate identity does not match the frozen generator")
    binary = identity.get("binary")
    if not isinstance(binary, dict) or not isinstance(binary.get("path"), str):
        raise ValueError("candidate identity is missing the binary identity")
    binary_path = Path(binary["path"])
    if not binary_path.is_file() or sha256(binary_path) != binary.get("sha256"):
        raise ValueError("changed or missing candidate binary")
    return identity, path


def _process_evidence(directory: Path, relative: Any, expected_relative: str,
                      workload: dict[str, Any], paths: dict[str, Path], mode: str,
                      generations: int, recorded: dict[str, Any],
                      binary_path: str) -> int | None:
    if not isinstance(relative, str) or not relative:
        raise ValueError("trial block is missing its raw process evidence path")
    candidate = Path(relative)
    if candidate.is_absolute() or ".." in candidate.parts:
        raise ValueError("raw process evidence path escapes the trial directory")
    if relative != expected_relative:
        raise ValueError("trial row points to the wrong raw process evidence")
    process_path = directory / candidate
    process = _load_object(process_path, "raw process evidence")
    for key in ("started", "stdout", "stderr", "output_json_path", "output_json_text"):
        if not isinstance(process.get(key), str):
            raise ValueError(f"raw process evidence has invalid {key}: {relative}")
    for key in ("gpu_before", "gpu_after"):
        if not isinstance(process.get(key), dict):
            raise ValueError(f"raw process evidence has invalid {key}: {relative}")
    returncode = process.get("returncode")
    if isinstance(returncode, bool) or returncode != 0:
        raise ValueError(f"raw process evidence did not complete successfully: {relative}")
    wall_ms = _number(process.get("wall_ms"), f"{relative} wall_ms")
    command = process.get("command")
    output_path = process_path.with_name(f"{mode}.output.json")
    native = measurement_command(workload, paths, Path(binary_path), output_path, mode)
    direct_command = ["rtk", "proxy", *native]
    timed_command = ["/usr/bin/time", "-v", "-o",
                     str(process_path.with_name(f"{mode}.time.txt")), "--", *direct_command]
    if command not in (direct_command, timed_command):
        raise ValueError(f"raw process evidence has a mismatched binary command: {relative}")
    if process.get("output_json_path") != str(output_path.resolve()):
        raise ValueError(f"raw process evidence has a mismatched output path: {relative}")
    output_json = process.get("output_json")
    output_text = process.get("output_json_text")
    if not isinstance(output_json, dict) or not isinstance(output_text, str):
        raise ValueError(f"raw process evidence is missing candidate output: {relative}")
    try:
        if json.loads(output_text) != output_json:
            raise ValueError(f"raw process output text and JSON disagree: {relative}")
    except json.JSONDecodeError as error:
        raise ValueError(f"raw process output text is invalid JSON: {relative}") from error
    if not output_path.is_file() or output_path.read_text(encoding="utf-8") != output_text:
        raise ValueError(f"raw candidate output is missing or mismatched: {relative}")
    extracted = extract_measurements(output_json, generations, wall_ms)
    if recorded != extracted:
        raise ValueError(f"trial row does not match raw process evidence: {relative}")
    rss = process.get("process_max_rss_kib")
    raw_rss = (_max_rss(process_path.with_name(f"{mode}.time.txt"))
               if command == timed_command else None)
    if rss != raw_rss:
        raise ValueError(f"host RSS differs from raw time output: {relative}")
    if rss is None:
        return None
    return _integer(rss, f"{relative} process_max_rss_kib")


def _summarize_partition(blocks: list[dict[str, Any]], population_size: int,
                         generations: int) -> dict[str, Any] | None:
    if not blocks:
        return None
    absolute: dict[str, list[float]] = {
        "canonical_cold": [], "process_wall": [], "evolve_call": [],
    }
    phases = {key: [[] for _ in range(generations)] for key in EXPECTED_PHASES}
    counters = {key: 0 for key in VARIATION_COUNTERS}
    rss_values: list[float] = []
    rss_present = []
    for block in blocks:
        scopes = block["numeric_scopes"]
        absolute["canonical_cold"].append(
            float(block["phase_times"]["generation_total_ms"][0]) +
            float(scopes["gpu_eval_init_ms"]))
        absolute["process_wall"].append(float(scopes["process_wall_ms"]))
        absolute["evolve_call"].append(float(scopes["evolve_call_ms"]))
        for key, series in block["phase_times"].items():
            for generation, value in enumerate(series):
                phases[key][generation].append(float(value))
        for key, series in block["variation_counters"].items():
            counters[key] += sum(series)
        rss = block["_host_process_max_rss_kib"]
        rss_present.append(rss is not None)
        if rss is not None:
            rss_values.append(float(rss))
    if any(rss_present) and not all(rss_present):
        raise ValueError("host process max RSS presence is inconsistent within a row partition")
    unchanged = counters["generation_repro_unchanged_children"]
    changed = counters["generation_repro_changed_children"]
    fallback = counters["generation_repro_fallback_children"]
    if fallback > unchanged:
        raise ValueError("fallback children must be a subset of unchanged variation outcomes")
    classified = unchanged + changed
    retained_per_block = population_size * generations
    return {
        "blocks": len(blocks),
        "absolute_times_ms": {key: _distribution(values) for key, values in absolute.items()},
        "phase_times_ms": {key: {
            "all_generation_observations": _distribution([
                value for generation_values in by_generation for value in generation_values]),
            "by_generation": [_distribution(values) for values in by_generation],
        } for key, by_generation in sorted(phases.items())},
        "variation": {
            "counter_totals": counters,
            "classified_variation_outcome_denominator": classified,
            "classified_variation_outcome_definition": (
                "changed_children + unchanged_children; operator outputs include the odd discarded "
                "crossover sibling and later attempted mutation outputs"),
            "fallback_fraction_of_classified_variation_outcomes": (
                fallback / classified if classified else None),
            "changed_fraction_of_classified_variation_outcomes": (
                changed / classified if classified else None),
            "retained_child_reference": {
                "formula": "population_size * generations",
                "per_block": retained_per_block,
                "across_blocks": retained_per_block * len(blocks),
                "is_variation_fraction_denominator": False,
            },
        },
        "host_process_max_rss_kib": _distribution(rss_values) if rss_values else None,
    }


def build_report(trials_path: Path, workload_manifest: dict[str, Any],
                 root: Path) -> dict[str, Any]:
    """Validate raw candidate evidence and produce a descriptive-only summary."""
    trials_path = trials_path.resolve()
    directory = trials_path.parent
    if trials_path.name != "trials.json":
        raise ValueError("report requires the runner's trials.json")
    paths = validate_manifest(workload_manifest, root.resolve())
    inventory, inventory_path = _validate_inventory(directory)
    identity, identity_path = _validate_identity(directory, workload_manifest)
    trials = _load_object(trials_path, "candidate trials")
    if trials.get("version") != TRIAL_VERSION:
        raise ValueError("unsupported candidate trial version")
    for key in ("warmup_blocks", "measured_blocks", "analysis_seed"):
        if trials.get(key) != workload_manifest.get(key):
            raise ValueError(f"candidate trials {key} does not match the frozen manifest")
    total_blocks = workload_manifest["warmup_blocks"] + workload_manifest["measured_blocks"]
    requested = _integer(trials.get("blocks_requested"), "blocks_requested", positive=True)
    if requested > total_blocks:
        raise ValueError("blocks_requested exceeds the frozen protocol")
    complete = trials.get("complete")
    if not isinstance(complete, bool) or complete is not (requested == total_blocks):
        raise ValueError("candidate trial completion marker is inconsistent")
    expected_status = ("candidate capture complete; comparison pending" if complete
                       else "incomplete pilot; comparison pending")
    if trials.get("status") != expected_status:
        raise ValueError("candidate trial status is inconsistent")
    workloads = {workload["id"]: workload for workload in workload_manifest["workloads"]}
    required = [f"{workload_id}/{mode}" for workload_id in workloads for mode in MODES]
    if trials.get("required_rows") != required:
        raise ValueError("candidate trials do not declare the complete five-mode workload matrix")
    rows = trials.get("rows")
    if not isinstance(rows, dict) or set(rows) != set(required):
        raise ValueError("candidate trials have missing or unexpected workload/mode rows")

    summaries: dict[str, Any] = {}
    binary_path = identity["binary"]["path"]
    workload_indices = {workload["id"]: index
                        for index, workload in enumerate(workload_manifest["workloads"])}
    for name in required:
        workload_id, mode = name.rsplit("/", 1)
        workload = workloads[workload_id]
        row = rows[name]
        if not isinstance(row, dict) or row.get("workload_sha256") != canonical_hash(workload):
            raise ValueError(f"candidate row changed frozen workload identity: {name}")
        blocks = row.get("blocks")
        if not isinstance(blocks, list) or len(blocks) != requested:
            raise ValueError(f"candidate row has incomplete block coverage: {name}")
        population_size = _option_integer(workload, "--population-size")
        generations = _option_integer(workload, "--generations")
        validated = []
        for expected_id, block in enumerate(blocks):
            if not isinstance(block, dict) or block.get("block_id") != expected_id:
                raise ValueError(f"candidate row has invalid block ordering: {name}")
            if block.get("warmup") is not (expected_id < workload_manifest["warmup_blocks"]):
                raise ValueError(f"candidate row has invalid warmup marker: {name}")
            recorded = {key: block.get(key) for key in
                        ("numeric_scopes", "phase_times", "variation_counters")}
            if (not isinstance(recorded["numeric_scopes"], dict) or
                    set(recorded["numeric_scopes"]) != EXPECTED_SCOPES or
                    not isinstance(recorded["phase_times"], dict) or
                    set(recorded["phase_times"]) != EXPECTED_PHASES or
                    not isinstance(recorded["variation_counters"], dict) or
                    set(recorded["variation_counters"]) != set(VARIATION_COUNTERS)):
                raise ValueError(f"candidate row has an inconsistent measurement schema: {name}")
            expected_process = (f"block-{expected_id:03d}/"
                                f"workload-{workload_indices[workload_id]:03d}/"
                                f"{mode}.process.json")
            rss = _process_evidence(directory, block.get("process"), expected_process,
                                    workload, paths[workload_id], mode, generations,
                                    recorded, binary_path)
            for generation, (fallback, unchanged) in enumerate(zip(
                    recorded["variation_counters"]["generation_repro_fallback_children"],
                    recorded["variation_counters"]["generation_repro_unchanged_children"])):
                if fallback > unchanged:
                    raise ValueError(
                        f"fallback children exceed unchanged outcomes in {name} generation {generation}")
            validated.append({**block, "_host_process_max_rss_kib": rss})
        warmups = [block for block in validated if block["warmup"]]
        measured = [block for block in validated if not block["warmup"]]
        summaries[name] = {
            "workload": workload_id,
            "mode": mode,
            "measured": _summarize_partition(measured, population_size, generations),
            "warmup_descriptive": _summarize_partition(warmups, population_size, generations),
        }

    for workload_id in workloads:
        cpu = summaries[f"{workload_id}/cpu"]
        for mode in MODES:
            row = summaries[f"{workload_id}/{mode}"]
            for partition in ("measured", "warmup_descriptive"):
                summary = row[partition]
                cpu_summary = cpu[partition]
                if summary is None or cpu_summary is None:
                    continue
                speedups = {}
                for scope in ("canonical_cold", "process_wall", "evolve_call"):
                    cpu_median = cpu_summary["absolute_times_ms"][scope]["median"]
                    mode_median = summary["absolute_times_ms"][scope]["median"]
                    speedups[scope] = cpu_median / mode_median if mode_median else None
                summary["candidate_speedup_vs_cpu"] = speedups

    return {
        "version": VERSION,
        "status": ("candidate measurement summary complete" if complete
                   else "incomplete pilot; no acceptance conclusion"),
        "complete": complete,
        "scope": "candidate-only descriptive measurements",
        "comparison_or_acceptance_claim": None,
        "warmup_blocks_required": workload_manifest["warmup_blocks"],
        "measured_blocks_required": workload_manifest["measured_blocks"],
        "blocks_captured": requested,
        "evidence": {
            "workload_manifest_canonical_sha256": canonical_hash(workload_manifest),
            "trials_sha256": sha256(trials_path),
            "identity_sha256": sha256(identity_path),
            "candidate_binary_sha256": identity["binary"]["sha256"],
            "raw_sha256_inventory_sha256": sha256(inventory_path),
            "validated_raw_files": len(inventory),
        },
        "rows": summaries,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trials", type=Path)
    parser.add_argument("--workloads", type=Path, required=True,
                        help="independently frozen compiled workload manifest")
    parser.add_argument("--root", type=Path,
                        help="artifact root; defaults to the workload manifest directory")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    trials = args.trials.resolve()
    output = args.output.resolve()
    if output.is_relative_to(trials.parent):
        parser.error("report output must be outside the immutable trial directory")
    manifest_path = args.workloads.resolve()
    manifest = _load_object(manifest_path, "frozen workload manifest")
    report = build_report(trials, manifest,
                          args.root.resolve() if args.root else manifest_path.parent)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
