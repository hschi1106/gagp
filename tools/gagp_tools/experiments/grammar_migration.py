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

from gagp_tools.shared.migration_protocol import STRICT_PROTOCOL, representative


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


def validate_grammar_dependencies(artifact: dict[str, Any], root: Path) -> None:
    """Freeze the complete file-import closure, including mode overrides."""
    declared = artifact.get("dependencies", [])
    if not isinstance(declared, list):
        raise ValueError("grammar dependencies must be artifact records")
    hashes = {}
    for dependency in declared:
        if not isinstance(dependency, dict) or not isinstance(dependency.get("path"), str):
            raise ValueError("grammar dependencies must be artifact records")
        path = (root / dependency["path"]).resolve()
        if path in hashes or not path.is_file() or sha256(path) != dependency.get("sha256"):
            raise ValueError("duplicate, changed or missing grammar dependency")
        hashes[path] = dependency["sha256"]
    visited, active = set(), set()

    def visit(path: Path) -> None:
        if path in active:
            raise ValueError("cyclic grammar imports")
        if path in visited:
            return
        if not path.is_file():
            raise ValueError(f"missing grammar import: {path}")
        document = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(document, dict) or not isinstance(document.get("imports", []), list):
            raise ValueError("grammar imports require an object and a path list")
        active.add(path)
        for imported in document.get("imports", []):
            if (not isinstance(imported, str) or not imported or
                    Path(imported).is_absolute() or "://" in imported or "\0" in imported):
                raise ValueError("grammar imports must be local relative paths")
            visit((path.parent / imported).resolve())
        active.remove(path)
        visited.add(path)

    entry = (root / artifact["path"]).resolve()
    visit(entry)
    if visited - {entry} != set(hashes):
        raise ValueError("grammar dependencies must exactly cover the import closure")


def grammar_artifact_identity(artifact: dict[str, Any]) -> dict[str, Any]:
    return {"sha256": artifact["sha256"],
            "dependencies": sorted((d["path"], d["sha256"])
                                   for d in artifact.get("dependencies", []))}


def workload_id(workload: dict[str, Any]) -> str:
    return workload.get("pair_id", workload.get("id", ""))


def workload_for_role(workload: dict[str, Any], role: str) -> dict[str, Any]:
    """Return one executable side while retaining common paired metadata."""
    if "pair_id" not in workload:
        return workload
    side = workload[role]
    return {key: value for key, value in workload.items()
            if key not in ("before", "after")} | side


def workload_hash(workload: dict[str, Any], role: str) -> str:
    if "pair_id" not in workload:
        return canonical_hash(workload)
    return canonical_hash(workload_for_role(workload, role))


def uses_cpu_donor_replay(workload: dict[str, Any], role: str) -> bool:
    """Whether one steady-reproduction side uses the retired CPU tape runner."""
    if workload.get("measurement") != "steady_repro":
        return False
    if "pair_id" not in workload:
        return True
    return workload_for_role(workload, role).get("cpu_repro_source") == "donor_replay"


def validate_manifest(manifest: dict[str, Any], root: Path) -> None:
    version = manifest.get("version")
    if version not in ("migration-workloads-v1", "migration-workloads-v2"):
        raise ValueError("unsupported workload manifest version")
    reduced = representative(manifest.get("acceptance_protocol", STRICT_PROTOCOL))
    warmups, counts = (1, (3, 5)) if reduced else (3, (15, 30))
    if manifest.get("warmup_blocks") != warmups or manifest.get("measured_blocks") not in counts:
        raise ValueError(f"require {warmups} warmup blocks and measured blocks in {counts}")
    selected = manifest["modes"]
    required_modes = BASE_MODES[:4] if reduced else BASE_MODES
    if not set(required_modes) <= set(selected) or set(selected) - set(MODES) or len(selected) != len(set(selected)):
        raise ValueError("require all protocol modes, with unique supported optional modes")
    if not isinstance(manifest["analysis_seed"], int) or isinstance(manifest["analysis_seed"], bool):
        raise ValueError("analysis_seed must be an integer")
    seen = set()
    logical_workloads = {}
    logical_measurements = set()
    for workload in manifest["workloads"]:
        if workload.get("measurement", "evolution") not in ("evolution", "steady_eval", "steady_repro"):
            raise ValueError("unsupported workload measurement")
        if workload.get("measurement") in ("steady_eval", "steady_repro"):
            session_warmups, session_trials = (1, (1,)) if reduced else (3, (15, 30))
            if workload.get("session_warmups") != session_warmups or workload.get("session_trials") not in session_trials:
                raise ValueError("steady session repetitions differ from the selected protocol")
        if version == "migration-workloads-v2":
            if set(workload) & {"id", "args", "cases", "snapshot", "grammar", "grammar_by_mode", "donor_tape"}:
                raise ValueError("v2 workload artifacts and args must be inside before and after")
            if not isinstance(workload.get("before"), dict) or not isinstance(workload.get("after"), dict):
                raise ValueError("v2 workload requires before and after sides")
            comparison = workload.get("comparison")
            if not isinstance(comparison, dict):
                raise ValueError("v2 workload requires comparison metadata")
            comparison_fields = {"gate_eligible", "mapping_kind", "case_identity",
                                 "limits_identity", "program_identity", "limitations",
                                 "evidence"}
            if set(comparison) != comparison_fields:
                raise ValueError("v2 comparison metadata fields differ from the frozen schema")
            if comparison["gate_eligible"] is not True:
                raise ValueError("v2 workload must be explicitly eligible for the paired gate")
            if comparison["mapping_kind"] not in (
                    "exact-runtime-replay", "typed-search-space",
                    "typed-partition-search-space"):
                raise ValueError("v2 workload has an unsupported mapping kind")
            if comparison["case_identity"] != "exact" or comparison["limits_identity"] != "exact":
                raise ValueError("v2 paired gate requires exact cases and execution/search limits")
            if comparison["program_identity"] not in ("exact", "mapped"):
                raise ValueError("v2 workload has an unsupported program identity")
            if ((comparison["mapping_kind"] == "exact-runtime-replay") !=
                    (comparison["program_identity"] == "exact")):
                raise ValueError("v2 mapping kind and program identity disagree")
            if (not isinstance(comparison["limitations"], list) or
                    not all(isinstance(item, str) and item
                            for item in comparison["limitations"])):
                raise ValueError("v2 comparison limitations must be nonempty strings")
            if (not isinstance(comparison["evidence"], list) or
                    not comparison["evidence"] or
                    not all(isinstance(item, str) and item
                            for item in comparison["evidence"])):
                raise ValueError("v2 comparison requires nonempty evidence references")
            common = {"pair_id", "logical_id", "measurement", "session_warmups", "session_trials"}
            if set(workload["before"]) & common or set(workload["after"]) & common:
                raise ValueError("v2 pair and measurement metadata must be common to both sides")
        elif "pair_id" in workload or "before" in workload or "after" in workload:
            raise ValueError("v1 workload cannot contain paired v2 sides")
        name = workload_id(workload)
        if not name or name in seen:
            raise ValueError("workload IDs or pair IDs must be nonempty and unique")
        seen.add(name)
        roles = ("before", "after") if version == "migration-workloads-v2" else ("before",)
        for role in roles:
            side = workload_for_role(workload, role)
            args = side["args"]
            prefix = f"{role} " if version == "migration-workloads-v2" else ""
            if not isinstance(args, list) or len(args) % 2:
                raise ValueError(f"{prefix}workload args must be option/value pairs")
            options = args[::2]
            allowed = {"--population-size", "--generations", "--blocksize", "--seed", "--fuel",
                       "--penalty", "--selection-pressure", "--mutation-rate", "--mutation-subtree-prob",
                       "--max-expr-depth", "--max-stmts-per-block", "--max-total-nodes",
                       "--source-max-total-nodes", "--source-max-expr-depth",
                       "--minimum-dc-frames", "--normalize-typed-storage", "--population-roots",
                       "--max-for-k", "--max-call-args", "--skip-final-eval", "--retain-final-population"}
            if len(set(options)) != len(options) or set(options) - allowed:
                raise ValueError(f"duplicate or unsupported {prefix}workload option")
            source_options = {"--source-max-total-nodes", "--source-max-expr-depth"}
            if "--population-roots" in options:
                roots = args[options.index("--population-roots") * 2 + 1].split(",")
                if (version != "migration-workloads-v2" or role != "after" or
                        "grammar" not in side or not 1 <= len(roots) <= 8 or
                        any(not name or name != name.strip() for name in roots) or
                        len(set(roots)) != len(roots)):
                    raise ValueError("population roots require a v2 candidate grammar and one to eight unique nonempty roots")
            if "--normalize-typed-storage" in options:
                values = dict(zip(options, args[1::2]))
                if (version != "migration-workloads-v2" or role != "after" or
                        values["--normalize-typed-storage"] not in {"on", "off"}):
                    raise ValueError("typed storage normalization requires a v2 candidate and on/off")
            if "--minimum-dc-frames" in options:
                values = dict(zip(options, args[1::2]))
                if (version != "migration-workloads-v2" or role != "after" or
                        "grammar" not in side or not 1 <= int(values["--minimum-dc-frames"]) <= 65535):
                    raise ValueError("minimum DC frames requires a v2 candidate grammar and 1..65535 frames")
            if set(options) & source_options:
                if (version != "migration-workloads-v2" or role != "after" or
                        "grammar" not in side or not source_options <= set(options)):
                    raise ValueError("source resource limits require a v2 candidate grammar and both limits")
                values = dict(zip(options, args[1::2]))
                if any(int(values[option]) <= 0 for option in source_options):
                    raise ValueError("source resource limits must be positive")
            if not {"--population-size", "--generations", "--blocksize", "--seed", "--fuel"} <= set(options):
                raise ValueError(f"{prefix}workload must freeze population, generations, blocksize, seed and fuel")
            if workload.get("measurement") == "steady_repro" and version == "migration-workloads-v2":
                source = side.get("cpu_repro_source")
                if source not in ("adapter", "donor_replay"):
                    raise ValueError(f"{role} steady reproduction requires an explicit cpu_repro_source")
                if source == "adapter" and "donor_tape" in side:
                    raise ValueError(f"{role} adapter reproduction cannot include a donor tape")
                if source == "adapter" and "grammar" not in side:
                    raise ValueError(f"{role} adapter reproduction requires a grammar")
            elif "cpu_repro_source" in side:
                raise ValueError(f"{prefix}cpu_repro_source is only valid for v2 steady reproduction")
            overrides = side.get("grammar_by_mode", {})
            if "grammar_by_mode" in side:
                if (version != "migration-workloads-v2" or role != "after" or
                        "grammar" not in side or not isinstance(overrides, dict) or
                        not overrides or set(overrides) - set(selected)):
                    raise ValueError("grammar_by_mode requires a v2 candidate default grammar and selected modes")
                for mode, artifact in overrides.items():
                    if (not isinstance(artifact, dict) or
                            not isinstance(artifact.get("path"), str) or
                            not (root / artifact["path"]).is_file() or
                            sha256(root / artifact["path"]) != artifact.get("sha256")):
                        raise ValueError(f"changed or missing after grammar_by_mode {mode} artifact for {name}")
                    validate_grammar_dependencies(artifact, root)
            kinds = ["cases", "snapshot", "grammar"]
            if uses_cpu_donor_replay(workload, role):
                kinds.append("donor_tape")
            for kind in kinds:
                artifact = side.get(kind)
                if artifact is None and kind == "grammar":
                    continue
                path = root / artifact["path"] if artifact is not None else None
                if (artifact is None or not path.is_file()
                        or sha256(path) != artifact["sha256"]):
                    label = f"{role} {kind}" if version == "migration-workloads-v2" else kind
                    raise ValueError(f"changed or missing {label} artifact for {name}")
                if kind == "grammar":
                    validate_grammar_dependencies(artifact, root)
        if reduced:
            logical_id = workload.get("logical_id", name)
            if not isinstance(logical_id, str) or not logical_id.strip():
                raise ValueError("logical workload ID must be a nonempty string")
            # Different scopes share one workload only when their actual inputs
            # and search settings agree. Generation count and output retention
            # may differ for the short evolution anchor.
            identity = {}
            for role in roles:
                side = workload_for_role(workload, role)
                options = dict(zip(side["args"][::2], side["args"][1::2]))
                for option in ("--generations", "--skip-final-eval", "--retain-final-population"):
                    options.pop(option, None)
                identity[role] = {
                    "options": options,
                    "grammar_by_mode": {mode: grammar_artifact_identity(artifact) for mode, artifact in
                                        side.get("grammar_by_mode", {}).items()},
                    "grammar_dependencies": grammar_artifact_identity(side["grammar"]) if "grammar" in side else None,
                    "artifacts": {kind: side[kind]["sha256"] if kind in side else None
                                  for kind in ("cases", "snapshot", "grammar")},
                }
            if logical_id in logical_workloads and logical_workloads[logical_id] != identity:
                raise ValueError("logical workload scopes must preserve inputs and search settings")
            logical_workloads[logical_id] = identity
            measurement = (logical_id, workload.get("measurement", "evolution"))
            if measurement in logical_measurements:
                raise ValueError("duplicate measurement for logical workload")
            logical_measurements.add(measurement)
            if len(logical_workloads) > 6:
                raise ValueError("representative protocol permits at most six logical workloads")
    if not seen:
        raise ValueError("empty workload matrix")


def extract_scopes(payload: dict[str, Any], wall_ms: float,
                   protocol: str = STRICT_PROTOCOL) -> dict[str, float]:
    reduced = representative(protocol)
    if payload.get("diagnostic_only") is True:
        raise ValueError("diagnostic observations cannot be used as timing evidence")
    steady_formats = {"migration-steady-eval-v1": "steady_eval",
                      "migration-steady-reproduction-v1": "steady_reproduction",
                      "migration-steady-cpu-reproduction-v1": "steady_reproduction"}
    if payload.get("format_version") in steady_formats:
        warmups, trials = payload["warmups"], payload["measured_trials"]
        samples = payload["samples"]
        valid_counts = (warmups == 1 and trials == 1) if reduced else (warmups >= 3 and trials >= 15)
        if not valid_counts or len(samples) != warmups + trials:
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
                        result_path: Path, cpu_reproduction: Path | None = None,
                        role: str = "before") -> list[str]:
    cpu_donor_replay = uses_cpu_donor_replay(workload, role)
    workload = workload_for_role(workload, role)
    measurement = workload.get("measurement", "evolution")
    if measurement == "steady_repro" and mode == "cpu" and cpu_donor_replay:
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
    grammar = workload.get("grammar_by_mode", {}).get(mode, workload.get("grammar"))
    if grammar:
        command.extend(["--grammar-definition", str((root / grammar["path"]).resolve())])
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
        replay_roles = {role for role in binaries for workload in manifest["workloads"]
                        if uses_cpu_donor_replay(workload, role)}
        if set(cpu_reproduction) != replay_roles:
            raise ValueError("CPU donor replay executable roles differ from the frozen workload protocol")
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
        if "acceptance_protocol" in manifest:
            report["acceptance_protocol"] = manifest["acceptance_protocol"]
        for workload in manifest["workloads"]:
            for mode in workload_modes(workload, manifest["modes"]):
                for scope in workload_scopes(workload):
                    name = f"{workload_id(workload)}/{mode}/{scope}"
                    report["required_rows"].append(name)
                    report["rows"][name] = {"workload_before_sha256": workload_hash(workload, "before"),
                        "workload_after_sha256": workload_hash(workload, "after"), "scope": scope,
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
                names = [f"{workload_id(workload)}/{mode}/{scope}" for mode in modes for scope in workload_scopes(workload)]
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
                                                  cpu_reproduction.get(role), role)
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
                    values[role, mode] = extract_scopes(json.loads(result_path.read_text()), wall_ms,
                        manifest.get("acceptance_protocol", STRICT_PROTOCOL))
                    if set(values[role, mode]) != set(workload_scopes(workload)):
                        raise ValueError("binary returned the wrong measurement scopes")
                write_json(directory / "order.json", order)
                for mode in modes:
                    for scope in workload_scopes(workload):
                        name = f"{workload_id(workload)}/{mode}/{scope}"
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
