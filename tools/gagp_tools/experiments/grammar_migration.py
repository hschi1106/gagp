"""Run paired fresh-process grammar migration measurements from a frozen manifest."""
from __future__ import annotations

import argparse
import hashlib
import fcntl
import json
import math
import os
import random
import statistics
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


MODES = {
    "cpu": ["--engine", "cpu", "--repro-backend", "cpu", "--repro-overlap", "off"],
    "gpu_eval": ["--engine", "gpu", "--repro-backend", "cpu", "--repro-overlap", "off"],
    "gpu_repro": ["--engine", "gpu", "--repro-backend", "gpu", "--repro-overlap", "off"],
    "gpu_repro_overlap": ["--engine", "gpu", "--repro-backend", "gpu", "--repro-overlap", "on"],
    "cpu_eval_gpu_repro": ["--engine", "cpu", "--repro-backend", "gpu", "--repro-overlap", "off"],
}
BASE_MODES = tuple(MODES)
for _engine in ("cpu", "gpu"):
    for _ablation in ("gpu_selection", "gpu_candidates", "gpu_coupled_donor"):
        MODES[f"{_engine}_{_ablation}"] = ["--engine", _engine, "--repro-backend", "cpu",
            "--repro-overlap", "off", "--cpu-repro-ablation", _ablation]
SCOPES = ("cli_wall", "canonical_cold", "evolve_call")


def workload_modes(workload: dict[str, Any], selected: list[str] | None = None) -> dict[str, list[str]]:
    if workload.get("measurement") == "steady_repro":
        return {name: MODES[name] for name in ("cpu", "gpu_repro")}
    if workload.get("measurement", "evolution") == "steady_eval":
        return {name: MODES[name] for name in ("cpu", "gpu_eval")}
    return {name: MODES[name] for name in (selected if selected is not None else BASE_MODES)}


def workload_scopes(workload: dict[str, Any]) -> tuple[str, ...]:
    if workload.get("measurement") == "steady_repro":
        return ("steady_reproduction",)
    return ("steady_eval",) if workload.get("measurement") == "steady_eval" else SCOPES


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical_hash(value: Any) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def validate_manifest(manifest: dict[str, Any], root: Path) -> None:
    if manifest.get("version") != "migration-workloads-v1":
        raise ValueError("unsupported workload manifest version")
    if manifest.get("warmup_blocks") != 3 or manifest.get("measured_blocks") not in (15, 30):
        raise ValueError("require 3 warmup blocks and 15 or 30 measured blocks")
    selected = manifest["modes"]
    if not set(BASE_MODES) <= set(selected) or set(selected) - set(MODES) or len(selected) != len(set(selected)):
        raise ValueError("require all five base modes, with unique supported optional modes")
    if not isinstance(manifest["analysis_seed"], int) or isinstance(manifest["analysis_seed"], bool):
        raise ValueError("analysis_seed must be an integer")
    seen = set()
    for workload in manifest["workloads"]:
        if workload.get("measurement", "evolution") not in ("evolution", "steady_eval", "steady_repro"):
            raise ValueError("unsupported workload measurement")
        if workload.get("measurement") in ("steady_eval", "steady_repro"):
            if workload.get("session_warmups") != 3 or workload.get("session_trials") not in (15, 30):
                raise ValueError("steady evaluation requires 3 session warmups and 15 or 30 session trials")
        name = workload["id"]
        if not name or name in seen:
            raise ValueError("workload IDs must be nonempty and unique")
        seen.add(name)
        args = workload["args"]
        if not isinstance(args, list) or len(args) % 2:
            raise ValueError("workload args must be option/value pairs")
        options = args[::2]
        allowed = {"--population-size", "--generations", "--blocksize", "--seed", "--fuel",
                   "--penalty", "--selection-pressure", "--mutation-rate", "--mutation-subtree-prob",
                   "--max-expr-depth", "--max-stmts-per-block", "--max-total-nodes",
                   "--max-for-k", "--max-call-args", "--skip-final-eval", "--retain-final-population"}
        if len(set(options)) != len(options) or set(options) - allowed:
            raise ValueError("duplicate or unsupported workload option")
        if not {"--population-size", "--generations", "--blocksize", "--seed", "--fuel"} <= set(options):
            raise ValueError("workload must freeze population, generations, blocksize, seed and fuel")
        kinds = ("cases", "snapshot", "grammar", "donor_tape") if workload.get("measurement") == "steady_repro" else ("cases", "snapshot", "grammar")
        for kind in kinds:
            artifact = workload.get(kind)
            if artifact is None and kind == "grammar":
                continue
            if artifact is None or sha256(root / artifact["path"]) != artifact["sha256"]:
                raise ValueError(f"changed or missing {kind} artifact for {name}")
    if not seen:
        raise ValueError("empty workload matrix")


def extract_scopes(payload: dict[str, Any], wall_ms: float) -> dict[str, float]:
    steady_formats = {"migration-steady-eval-v1": "steady_eval",
                      "migration-steady-reproduction-v1": "steady_reproduction",
                      "migration-steady-cpu-reproduction-v1": "steady_reproduction"}
    if payload.get("format_version") in steady_formats:
        warmups, trials = payload["warmups"], payload["measured_trials"]
        samples = payload["samples"]
        if warmups < 3 or trials < 15 or len(samples) != warmups + trials:
            raise ValueError("incomplete steady session")
        for i, sample in enumerate(samples):
            if sample["index"] != i or sample["warmup"] is not (i < warmups):
                raise ValueError("steady samples have invalid ordering or warmup markers")
            value = sample["call_ms"]
            if isinstance(value, bool) or not isinstance(value, (float, int)) or not math.isfinite(value) or value <= 0:
                raise ValueError("steady samples require finite positive timings")
        # One session median is one observation in its outer paired block.
        # Individual calls within a session are not independent trial blocks.
        return {steady_formats[payload["format_version"]]: statistics.median(s["call_ms"] for s in samples[warmups:])}
    if payload.get("format_version") != "migration-run-v1" or not payload.get("generations"):
        raise ValueError("invalid migration run output")
    # Canonical timing explicitly adds only disjoint cold session init to the
    # measured generation total. Never sum overlapping reproduction subphases.
    return {"cli_wall": wall_ms,
            "canonical_cold": payload["generations"][0]["total_ms"] + payload["gpu_eval_init_ms"],
            "evolve_call": payload["evolve_call_ms"]}


def write_json(path: Path, payload: Any) -> None:
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(json.dumps(payload, indent=2) + "\n")
    temp.replace(path)


def gpu_state() -> dict[str, Any]:
    result = subprocess.run(["nvidia-smi", "--query-gpu=uuid,index,utilization.gpu,memory.used,clocks.sm,clocks.mem,temperature.gpu",
                             "--format=csv,noheader"], capture_output=True, text=True)
    return {"returncode": result.returncode, "stdout": result.stdout, "stderr": result.stderr}


def measurement_command(workload: dict[str, Any], mode: str, binary: Path, root: Path,
                        result_path: Path, cpu_reproduction: Path | None = None) -> list[str]:
    measurement = workload.get("measurement", "evolution")
    if measurement == "steady_repro" and mode == "cpu":
        if cpu_reproduction is None:
            raise ValueError("steady reproduction requires the CPU donor replay executable")
        command = [str(cpu_reproduction), "--steady", "--donor-tape",
                   str((root / workload["donor_tape"]["path"]).resolve())]
    else:
        action = {"evolution": "run", "steady_eval": "steady", "steady_repro": "repro-steady"}[measurement]
        command = [str(binary), "--action", action]
    command.extend(["--snapshot", str((root / workload["snapshot"]["path"]).resolve()), "--cases",
                    str((root / workload["cases"]["path"]).resolve()), *workload["args"],
                    *MODES[mode], "--out-json", str(result_path.resolve())])
    if measurement != "evolution":
        command.extend(["--warmups", str(workload["session_warmups"]), "--trials", str(workload["session_trials"])])
    if workload.get("grammar"):
        command.extend(["--grammar-config", str((root / workload["grammar"]["path"]).resolve())])
    return command


def run_manifest(manifest: dict[str, Any], root: Path, before: Path, after: Path | None,
                 output: Path, device: int, *, block_limit: int | None = None,
                 before_cpu_repro: Path | None = None, after_cpu_repro: Path | None = None,
                 resume: bool = False) -> dict[str, Any]:
    validate_manifest(manifest, root)
    # New run directory prevents overwriting or interpreting a partially observed
    # invocation as completed. Long-running invocations are polled by their caller.
    if not resume:
        output.mkdir(parents=True, exist_ok=False)
    # Kernel-owned lock excludes concurrent new runners. A legacy invocation
    # without this lock must be confirmed terminal before requesting recovery.
    with (output / ".runner.lock").open("a") as run_lock:
        fcntl.flock(run_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        binaries = {"before": before.resolve()}
        if after is not None:
            binaries["after"] = after.resolve()
        cpu_reproduction = {role: path.resolve() for role, path in
                            (("before", before_cpu_repro), ("after", after_cpu_repro)) if path is not None}
        if any(w.get("measurement") == "steady_repro" for w in manifest["workloads"]):
            if set(cpu_reproduction) != set(binaries):
                raise ValueError("each measured revision requires a CPU donor replay executable")
        identity = {"manifest_sha256": canonical_hash(manifest),
                    "binaries": {k: {"path": str(p), "sha256": sha256(p)} for k, p in binaries.items()},
                    "device": device, "cpu_affinity": sorted(os.sched_getaffinity(0)),
                    "environment": {key: os.environ.get(key) for key in
                                    ("OMP_NUM_THREADS", "CUDA_VISIBLE_DEVICES", "CUDA_DEVICE_ORDER")},
                    "gpu_start": gpu_state(), "manifest": manifest}
        if cpu_reproduction:
            identity["cpu_reproduction_binaries"] = {k: {"path": str(p), "sha256": sha256(p)} for k, p in cpu_reproduction.items()}
        if resume:
            previous_identity = json.loads((output / "identity.json").read_text())
            for key in ("manifest_sha256", "manifest", "binaries", "device", "cpu_affinity", "environment", "cpu_reproduction_binaries"):
                if previous_identity.get(key) != identity.get(key):
                    raise ValueError(f"cannot resume with changed {key}")
            from gagp_tools.reports.audit_migration_trials import audit_trials
            audit = audit_trials(output, manifest, root)
            if audit["status"] == "complete":
                return json.loads((output / "trials.json").read_text())
        else:
            write_json(output / "identity.json", identity)
        report = {"version": "grammar-migration-trials-v1" if after else "grammar-migration-baseline-trials-v1",
                  "analysis_seed": manifest["analysis_seed"],
                  "required_rows": [], "rows": {}}
        for workload in manifest["workloads"]:
            for mode in workload_modes(workload, manifest["modes"]):
                for scope in workload_scopes(workload):
                    name = f"{workload['id']}/{mode}/{scope}"
                    report["required_rows"].append(name)
                    report["rows"][name] = {"workload_before_sha256": canonical_hash(workload),
                        "workload_after_sha256": canonical_hash(workload), "scope": scope,
                        "timing_source": "canonical_cold_disjoint" if scope == "canonical_cold" else "direct",
                        "blocks": []}
        if resume:
            previous = json.loads((output / "trials.json").read_text())
            for key in ("version", "analysis_seed", "required_rows"):
                if previous[key] != report[key]:
                    raise ValueError(f"cannot resume with changed report {key}")
            report = previous
            recovery = {"version": "migration-recovery-v1", "started": datetime.now(timezone.utc).isoformat(),
                        "prior_trials_sha256": sha256(output / "trials.json"), "audit": audit,
                        "policy": "retain completed blocks; replace only uncommitted workload blocks; preserve every partial process record"}
            recovery_index = len(list(output.glob("recovery-*.json")))
            write_json(output / f"recovery-{recovery_index:03d}.json", recovery)
            if (output / "raw-sha256.json").exists():
                (output / "raw-sha256.json").rename(output / f"recovery-{recovery_index:03d}.prior-hashes.json")
        else:
            write_json(output / "trials.json", report)
        rng = random.Random(manifest["analysis_seed"])
        count = manifest["warmup_blocks"] + manifest["measured_blocks"]
        if block_limit is not None:
            if block_limit <= 0:
                raise ValueError("block limit must be positive")
            count = min(count, block_limit)
        env = dict(os.environ, GAGP_CUDA_DEVICE=str(device))
        for block in range(count):
            workloads = list(enumerate(manifest["workloads"]))
            rng.shuffle(workloads)
            for index, workload in workloads:
                directory = output / f"block-{block:03d}" / f"workload-{index:03d}"
                modes = workload_modes(workload, manifest["modes"])
                order = [(role, mode) for role in binaries for mode in modes]
                rng.shuffle(order)
                names = [f"{workload['id']}/{mode}/{scope}" for mode in modes for scope in workload_scopes(workload)]
                present = [any(b["block_id"] == block for b in report["rows"][name]["blocks"]) for name in names]
                if all(present):
                    continue
                if any(present):
                    raise ValueError("partially committed workload block cannot be resumed")
                if directory.exists():
                    if not resume:
                        raise ValueError("unexpected pre-existing workload directory")
                    archive = output / "failed-attempts" / f"block-{block:03d}" / f"workload-{index:03d}"
                    archive.mkdir(parents=True, exist_ok=True)
                    attempt = archive / f"attempt-{len(list(archive.glob('attempt-*'))):03d}"
                    directory.rename(attempt)
                    processes = [json.loads(p.read_text()) for p in attempt.glob("*.process.json")]
                    write_json(attempt / "replacement.json", {"reason": "failed_process" if any(p["returncode"] for p in processes)
                        else "interrupted_uncommitted_workload_block", "replaced_path": str(directory),
                        "files_sha256": {str(p.relative_to(attempt)): sha256(p) for p in sorted(attempt.rglob("*.json"))}})
                directory.mkdir(parents=True)
                values = {}
                for role, mode in order:
                    result_path = directory / f"{role}-{mode}.json"
                    command = measurement_command(workload, mode, binaries[role], root, result_path,
                                                  cpu_reproduction.get(role))
                    started = datetime.now(timezone.utc).isoformat()
                    before_gpu = gpu_state()
                    start = time.perf_counter()
                    result = subprocess.run(command, cwd=root, env=env, capture_output=True, text=True)
                    wall_ms = (time.perf_counter() - start) * 1000
                    raw = {"command": command, "started": started, "wall_ms": wall_ms,
                           "returncode": result.returncode, "stdout": result.stdout, "stderr": result.stderr,
                           "gpu_before": before_gpu, "gpu_after": gpu_state()}
                    write_json(directory / f"{role}-{mode}.process.json", raw)
                    if result.returncode:
                        raise RuntimeError(f"measurement failed; raw evidence retained at {directory}")
                    values[role, mode] = extract_scopes(json.loads(result_path.read_text()), wall_ms)
                    if set(values[role, mode]) != set(workload_scopes(workload)):
                        raise ValueError("binary returned the wrong measurement scopes")
                write_json(directory / "order.json", order)
                for mode in modes:
                    for scope in workload_scopes(workload):
                        name = f"{workload['id']}/{mode}/{scope}"
                        observation = {"block_id": block,
                            "warmup": block < manifest["warmup_blocks"],
                            "cpu_before_ms": values["before", "cpu"][scope],
                            "mode_before_ms": values["before", mode][scope]}
                        if after is not None:
                            observation.update({"cpu_after_ms": values["after", "cpu"][scope],
                                                "mode_after_ms": values["after", mode][scope]})
                        report["rows"][name]["blocks"].append(observation)
                write_json(output / "trials.json", report)
        write_json(output / "raw-sha256.json", {str(p.relative_to(output)): sha256(p)
                                                for p in sorted(output.rglob("*.json")) if p.name != "raw-sha256.json"})
        return report

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, help="omit for baseline-only capture; candidate comparison requires paired runs")
    parser.add_argument("--before-cpu-repro", type=Path)
    parser.add_argument("--after-cpu-repro", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--device", type=int, required=True)
    parser.add_argument("--block-limit", type=int, help="pilot only; incomplete runs remain pending")
    parser.add_argument("--resume", action="store_true", help="resume an audited, confirmed-terminal incomplete run")
    args = parser.parse_args()
    run_manifest(json.loads(args.manifest.read_text()), args.root.resolve(), args.before,
                 args.after, args.output, args.device, block_limit=args.block_limit,
                 before_cpu_repro=args.before_cpu_repro, after_cpu_repro=args.after_cpu_repro, resume=args.resume)


if __name__ == "__main__":
    main()
