"""Capture candidate timings for frozen compiled-grammar migration workloads."""
from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import math
import os
import random
import re
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from gagp_tools.experiments.grammar_migration import gpu_state


MODES = {
    "cpu": ["--engine", "cpu", "--repro-backend", "cpu", "--repro-overlap", "off"],
    "gpu_eval": ["--engine", "gpu", "--repro-backend", "cpu", "--repro-overlap", "off"],
    "gpu_repro": ["--engine", "gpu", "--repro-backend", "gpu", "--repro-overlap", "off"],
    "gpu_repro_overlap": ["--engine", "gpu", "--repro-backend", "gpu", "--repro-overlap", "on"],
    "cpu_eval_gpu_repro": ["--engine", "cpu", "--repro-backend", "gpu", "--repro-overlap", "off"],
}
VARIATION_COUNTERS = tuple(
    "generation_repro_" + suffix for suffix in (
        "crossover_attempts", "mutation_attempts", "contract_rejections",
        "budget_rejections", "generation_rejections", "acceptance_rejections",
        "fallback_children", "unchanged_children", "changed_children",
    )
)
ALLOWED_OPTIONS = {
    "--population-size", "--generations", "--blocksize", "--seed", "--fuel", "--penalty",
    "--selection-pressure", "--mutation-rate", "--mutation-subtree-prob", "--max-expr-depth",
    "--max-stmts-per-block", "--max-total-nodes", "--max-for-k", "--max-call-args",
    "--skip-final-eval", "--retain-final-population",
}
REQUIRED_OPTIONS = {"--population-size", "--generations", "--blocksize", "--seed", "--fuel"}
DIRECT_TIMING_SCOPES = (
    "load_ms", "evolve_call_ms", "init_population_ms", "gpu_eval_init_ms",
    "final_eval_ms", "total_ms",
)
EVALUATION_TIMINGS = (
    "cpu_compile_ms", "gpu_compile_ms", "gpu_eval_call_ms", "gpu_eval_pack_ms",
    "gpu_eval_launch_prep_ms", "gpu_eval_upload_ms", "gpu_eval_kernel_ms",
    "gpu_eval_copyback_ms", "gpu_eval_teardown_ms",
)
REPRODUCTION_TIMINGS = (
    "selection_ms", "crossover_ms", "mutation_ms", "prepare_inputs_ms", "setup_ms",
    "preprocess_ms", "pack_ms", "upload_ms", "kernel_ms", "copyback_ms", "decode_ms",
    "teardown_ms", "selection_kernel_ms", "variation_kernel_ms",
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical_hash(value: Any) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def _artifact_path(root: Path, artifact: dict[str, Any], label: str,
                   *, allow_absolute: bool = False) -> Path:
    raw = artifact.get("path")
    if not isinstance(raw, str) or not raw or (Path(raw).is_absolute() and not allow_absolute):
        raise ValueError(f"{label} path must be relative to the manifest")
    path = Path(raw).resolve() if Path(raw).is_absolute() else (root / raw).resolve()
    if not allow_absolute:
        try:
            path.relative_to(root.resolve())
        except ValueError as error:
            raise ValueError(f"{label} path escapes the manifest root") from error
    if not path.is_file() or sha256(path) != artifact.get("sha256"):
        raise ValueError(f"changed or missing {label} artifact")
    return path


def validate_manifest(manifest: dict[str, Any], root: Path) -> dict[str, dict[str, Path]]:
    """Validate the complete frozen input set once, before any output is created."""
    if manifest.get("version") != "compiled-migration-workloads-v1":
        raise ValueError("unsupported workload manifest version")
    if manifest.get("status") != "candidate preregistration; measurements pending":
        raise ValueError("workloads must remain a candidate preregistration before measurement")
    if manifest.get("warmup_blocks") != 3 or manifest.get("measured_blocks") != 15:
        raise ValueError("compiled migration requires 3 warmup and 15 measured blocks")
    if manifest.get("modes") != list(MODES):
        raise ValueError("compiled migration requires the five canonical modes")
    seed = manifest.get("analysis_seed")
    if isinstance(seed, bool) or not isinstance(seed, int):
        raise ValueError("analysis_seed must be an integer")
    generator = manifest.get("generator")
    if not isinstance(generator, dict):
        raise ValueError("manifest must identify the frozen population generator")
    resolved: dict[str, dict[str, Path]] = {"manifest": {
        "generator": _artifact_path(root, generator, "generator", allow_absolute=True)}}
    seen: set[str] = set()
    workloads = manifest.get("workloads")
    if not isinstance(workloads, list) or not workloads:
        raise ValueError("empty workload matrix")
    for workload in workloads:
        name = workload.get("id")
        if not isinstance(name, str) or not name or name in seen:
            raise ValueError("workload IDs must be nonempty and unique")
        seen.add(name)
        args = workload.get("args")
        if not isinstance(args, list) or len(args) % 2 or not all(isinstance(v, str) for v in args):
            raise ValueError(f"workload {name} args must be string option/value pairs")
        options = args[::2]
        if len(options) != len(set(options)) or set(options) - ALLOWED_OPTIONS:
            raise ValueError(f"workload {name} has duplicate or unsupported options")
        if not REQUIRED_OPTIONS <= set(options):
            raise ValueError(f"workload {name} does not freeze required execution options")
        resolved[name] = {}
        for kind in ("grammar", "cases", "snapshot"):
            artifact = workload.get(kind)
            if not isinstance(artifact, dict):
                raise ValueError(f"workload {name} is missing {kind}")
            resolved[name][kind] = _artifact_path(root, artifact, f"{name} {kind}")
    return resolved


def measurement_command(workload: dict[str, Any], paths: dict[str, Path], binary: Path,
                        result_path: Path, mode: str) -> list[str]:
    return [str(binary), "--action", "run", "--snapshot", str(paths["snapshot"]),
            "--grammar-definition", str(paths["grammar"]), "--cases", str(paths["cases"]),
            *workload["args"], *MODES[mode],
            "--timing", "all", "--out-json", str(result_path.resolve())]


def _numeric(value: Any, label: str, *, integer: bool = False) -> int | float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{label} must be a finite numeric value")
    if integer and (not isinstance(value, int) or value < 0):
        raise ValueError(f"{label} must be a non-negative integer")
    if not integer and value < 0:
        raise ValueError(f"{label} must be non-negative")
    return value


def extract_measurements(payload: dict[str, Any], expected_generations: int,
                         wall_ms: float) -> dict[str, Any]:
    """Extract direct timing scopes, every phase series, and all variation counters."""
    if not isinstance(payload, dict) or payload.get("format_version") != "migration-run-v1":
        raise ValueError("candidate output is not migration-run-v1")
    generations = payload.get("generations")
    if not isinstance(generations, list) or len(generations) != expected_generations:
        raise ValueError(f"candidate output must contain exactly {expected_generations} generations")
    scopes = {"process_wall_ms": _numeric(wall_ms, "process wall time")}
    missing_scopes = [key for key in DIRECT_TIMING_SCOPES if key not in payload]
    if missing_scopes:
        raise ValueError(f"candidate output is missing direct timing scopes: {missing_scopes}")
    for key, value in payload.items():
        if key not in ("format_version", "generations"):
            scopes[key] = _numeric(value, key)
    phases: dict[str, list[int | float]] = {}
    counters: dict[str, list[int]] = {}
    evaluation_keys: set[str] | None = None
    reproduction_keys: set[str] | None = None
    for index, generation in enumerate(generations):
        if not isinstance(generation, dict):
            raise ValueError(f"generation {index} must be an object")
        evaluation = generation.get("evaluation")
        reproduction = generation.get("reproduction")
        if not isinstance(evaluation, dict) or not isinstance(reproduction, dict):
            raise ValueError(f"generation {index} is missing phase timing objects")
        current_evaluation_keys = {key for key in evaluation if key.endswith("_ms")}
        current_reproduction_keys = {key for key in reproduction if key.endswith("_ms")}
        missing_evaluation = sorted(set(EVALUATION_TIMINGS) - current_evaluation_keys)
        missing_reproduction = sorted(set(REPRODUCTION_TIMINGS) - current_reproduction_keys)
        if missing_evaluation:
            raise ValueError(
                f"generation {index} is missing evaluation timings: {missing_evaluation}")
        if missing_reproduction:
            raise ValueError(
                f"generation {index} is missing reproduction timings: {missing_reproduction}")
        if evaluation_keys is None:
            evaluation_keys = current_evaluation_keys
            reproduction_keys = current_reproduction_keys
        elif (current_evaluation_keys != evaluation_keys or
              current_reproduction_keys != reproduction_keys):
            raise ValueError(f"generation {index} has inconsistent phase timing fields")
        phase_values = {
            "generation_eval_ms": generation.get("eval_ms"),
            "generation_repro_ms": generation.get("repro_ms"),
            "generation_total_ms": generation.get("total_ms"),
            **{f"generation_{key}": value for key, value in evaluation.items() if key.endswith("_ms")},
            **{f"generation_repro_{key}": value for key, value in reproduction.items() if key.endswith("_ms")},
        }
        for key, value in phase_values.items():
            phases.setdefault(key, []).append(_numeric(value, f"generations[{index}].{key}"))
        for key in VARIATION_COUNTERS:
            raw_key = key.removeprefix("generation_repro_")
            counters.setdefault(key, []).append(
                _numeric(reproduction.get(raw_key), f"generations[{index}].reproduction.{raw_key}",
                         integer=True))  # type: ignore[arg-type]
    if set(counters) != set(VARIATION_COUNTERS):
        missing = sorted(set(VARIATION_COUNTERS) - set(counters))
        raise ValueError(f"candidate output is missing variation counters: {missing}")
    if "generation_total_ms" not in phases:
        raise ValueError("candidate output is missing generation_total_ms")
    return {"numeric_scopes": scopes, "phase_times": phases, "variation_counters": counters}


def _write_json(path: Path, payload: Any) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w") as stream:
        json.dump(payload, stream, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def _max_rss(time_output: Path) -> int | None:
    if not time_output.exists():
        return None
    match = re.search(r"Maximum resident set size \(kbytes\):\s*(\d+)", time_output.read_text())
    return int(match.group(1)) if match else None


def run_manifest(manifest: dict[str, Any], root: Path, binary: Path, output: Path,
                 device: int, *, block_limit: int | None = None) -> dict[str, Any]:
    root = root.resolve()
    output = output.resolve()
    paths = validate_manifest(manifest, root)
    binary = binary.resolve()
    if not binary.is_file():
        raise ValueError("candidate binary is missing")
    binary_identity = {"path": str(binary), "sha256": sha256(binary)}
    total_blocks = manifest["warmup_blocks"] + manifest["measured_blocks"]
    if block_limit is not None and (isinstance(block_limit, bool) or block_limit <= 0):
        raise ValueError("block limit must be a positive integer")
    blocks_to_run = total_blocks if block_limit is None else min(block_limit, total_blocks)
    output.mkdir(parents=True, exist_ok=False)
    with (output / ".runner.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        identity = {
            "version": "compiled-migration-candidate-identity-v1",
            "manifest_sha256": canonical_hash(manifest),
            "binary": binary_identity,
            "generator": manifest["generator"],
            "device": device,
            "cpu_affinity": sorted(os.sched_getaffinity(0)),
            "environment": {key: os.environ.get(key) for key in
                            ("OMP_NUM_THREADS", "CUDA_VISIBLE_DEVICES", "CUDA_DEVICE_ORDER")},
            "gpu_start": gpu_state(),
        }
        _write_json(output / "identity.json", identity)
        report = {
            "version": "compiled-migration-candidate-trials-v1",
            "status": "incomplete pilot; comparison pending" if blocks_to_run < total_blocks
                      else "capture running; comparison pending",
            "complete": False,
            "analysis_seed": manifest["analysis_seed"],
            "warmup_blocks": manifest["warmup_blocks"],
            "measured_blocks": manifest["measured_blocks"],
            "blocks_requested": blocks_to_run,
            "required_rows": [f"{workload['id']}/{mode}" for workload in manifest["workloads"] for mode in MODES],
            "rows": {f"{workload['id']}/{mode}": {"workload_sha256": hashlib.sha256(
                json.dumps(workload, sort_keys=True, separators=(",", ":")).encode()).hexdigest(), "blocks": []}
                for workload in manifest["workloads"] for mode in MODES},
        }
        _write_json(output / "trials.json", report)
        rng = random.Random(manifest["analysis_seed"])
        env = dict(os.environ, GAGP_CUDA_DEVICE=str(device))
        time_binary = Path("/usr/bin/time")
        try:
            for block in range(blocks_to_run):
                indexed = list(enumerate(manifest["workloads"]))
                rng.shuffle(indexed)
                for workload_index, workload in indexed:
                    directory = output / f"block-{block:03d}" / f"workload-{workload_index:03d}"
                    directory.mkdir(parents=True)
                    mode_order = list(MODES)
                    rng.shuffle(mode_order)
                    _write_json(directory / "order.json", mode_order)
                    workload_measurements = {}
                    for mode in mode_order:
                        result_path = directory / f"{mode}.output.json"
                        time_path = directory / f"{mode}.time.txt"
                        native = measurement_command(workload, paths[workload["id"]], binary, result_path, mode)
                        launched = ["rtk", "proxy", *native]
                        if time_binary.is_file():
                            launched = [str(time_binary), "-v", "-o", str(time_path), "--", *launched]
                        before_gpu = gpu_state()
                        started = datetime.now(timezone.utc).isoformat()
                        start = time.perf_counter()
                        result = subprocess.run(launched, cwd=root, env=env, capture_output=True, text=True)
                        wall_ms = (time.perf_counter() - start) * 1000
                        output_text = result_path.read_text() if result_path.is_file() else None
                        try:
                            output_json = json.loads(output_text) if output_text is not None else None
                        except json.JSONDecodeError:
                            output_json = None
                        process = {"command": launched, "started": started, "wall_ms": wall_ms,
                                   "returncode": result.returncode, "stdout": result.stdout,
                                   "stderr": result.stderr, "output_json_path": str(result_path),
                                   "output_json": output_json, "output_json_text": output_text,
                                   "gpu_before": before_gpu, "gpu_after": gpu_state()}
                        rss = _max_rss(time_path)
                        if rss is not None:
                            process["process_max_rss_kib"] = rss
                        process_path = directory / f"{mode}.process.json"
                        _write_json(process_path, process)
                        if result.returncode:
                            raise RuntimeError(f"measurement failed; evidence retained at {process_path}")
                        if output_json is None:
                            raise ValueError(f"measurement produced no valid output JSON; evidence retained at {process_path}")
                        generations = int(workload["args"][workload["args"].index("--generations") + 1])
                        workload_measurements[mode] = extract_measurements(output_json, generations, wall_ms)
                    # Commit only complete five-mode workload blocks. Process evidence
                    # remains durable even when a later mode fails.
                    for mode in mode_order:
                        report["rows"][f"{workload['id']}/{mode}"]["blocks"].append({
                            "block_id": block, "warmup": block < manifest["warmup_blocks"],
                            "process": str((directory / f"{mode}.process.json").relative_to(output)),
                            **workload_measurements[mode]})
                    _write_json(output / "trials.json", report)
        except Exception:
            report["status"] = "failed; comparison pending"
            _write_json(output / "trials.json", report)
            raise
        report["complete"] = blocks_to_run == total_blocks
        report["status"] = ("candidate capture complete; comparison pending" if report["complete"]
                            else "incomplete pilot; comparison pending")
        _write_json(output / "trials.json", report)
        _write_json(output / "raw-sha256.json", {str(path.relative_to(output)): sha256(path)
                    for path in sorted(output.rglob("*")) if path.is_file() and path.name != "raw-sha256.json"})
        return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--root", type=Path, help="manifest artifact root; defaults to the manifest directory")
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--device", type=int, required=True)
    parser.add_argument("--block-limit", type=int, help="run an explicitly incomplete pilot")
    args = parser.parse_args()
    manifest_path = args.manifest.resolve()
    run_manifest(json.loads(manifest_path.read_text()),
                 args.root.resolve() if args.root else manifest_path.parent,
                 args.binary, args.output, args.device, block_limit=args.block_limit)


if __name__ == "__main__":
    main()
