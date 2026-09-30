from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import time

from .grammar import TASKS, definition

ROOT = Path(__file__).resolve().parents[4]
MODES = ("asgp_1t", "gagp_cpu", "gpu_eval", "gpu_repro", "gpu_overlap")
POPULATIONS = (1024, 2048, 4096, 8192)
CONTRACT = "fixed-asgp-v1"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def tree_hash(paths: list[Path], root: Path) -> str:
    values = [(str(p.relative_to(root)), sha256(p)) for p in sorted(paths)]
    return hashlib.sha256(json.dumps(values, separators=(",", ":")).encode()).hexdigest()


def asgp_source_hash(source: Path) -> str:
    files = [p for p in (source / "src").rglob("*") if p.is_file()]
    if not files:
        raise ValueError("ASGP source directory is empty")
    return tree_hash(files, source)


def save(path: Path, value) -> None:
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")


def capture(command: list[str]) -> str:
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, check=False)
    return result.stdout.strip() if result.returncode == 0 else result.stderr.strip()


def invoke(command: list[str], log: Path, timeout: float = 1800) -> dict:
    start = time.monotonic()
    status, code = "ok", None
    with log.open("w") as handle:
        try:
            result = subprocess.run(command, cwd=ROOT, stdout=handle, stderr=subprocess.STDOUT,
                                    timeout=timeout, check=False)
            code = result.returncode
            if code:
                status = "failed"
        except subprocess.TimeoutExpired:
            status = "timeout"
    return {"status": status, "returncode": code, "process_ms": (time.monotonic() - start) * 1000,
            "command": command, "log": str(log)}


def validate_rows(rows: list[dict], task: str, population: int, mode: str) -> list[dict]:
    if len(rows) != 4 or [r.get("rep") for r in rows] != [-1, 0, 1, 2]:
        raise ValueError("expected one warmup and exactly three measured repetitions")
    for row in rows:
        if (row.get("task"), row.get("population"), row.get("cases"), row.get("mode")) != (task, population, 1024, mode):
            raise ValueError("measurement identity mismatch")
        if not math.isfinite(row["generation_ms"]) or row["generation_ms"] <= 0:
            raise ValueError("invalid generation duration")
    return rows[1:]


def summarize(cells: list[dict]) -> list[dict]:
    output = []
    for cell in cells:
        row = {k: cell[k] for k in ("task", "population", "mode", "status")}
        if cell["status"] == "ok":
            samples = validate_rows(cell["rows"], cell["task"], cell["population"], cell["mode"])
            times = [r["generation_ms"] for r in samples]
            row.update(median_ms=statistics.median(times), min_ms=min(times), max_ms=max(times))
            row["phases_ms"] = {key: statistics.median(r[key] for r in samples)
                                for key in samples[0] if key.endswith("_ms")}
            row["variation"] = {key: [r[key] for r in samples] for key in samples[0]
                                if key.endswith("_outputs") or key.endswith("_rejections")}
        output.append(row)
    lookup = {(r["task"], r["population"], r["mode"]): r for r in output}
    for row in output:
        if row["status"] != "ok":
            continue
        for baseline, field in (("asgp_1t", "speedup_asgp_1t"), ("gagp_cpu", "speedup_gagp_cpu")):
            base = lookup.get((row["task"], row["population"], baseline), {})
            if base.get("status") == "ok":
                row[field] = base["median_ms"] / row["median_ms"]
        if row["mode"] == "gpu_overlap":
            base = lookup.get((row["task"], row["population"], "gpu_repro"), {})
            if base.get("status") == "ok":
                row["overlap_speedup"] = base["median_ms"] / row["median_ms"]
    return output


def adapter_hash() -> str:
    paths = [ROOT / "cpp/src/bench/fixed_asgp" / name for name in ("adapter.cpp", "adapter.hpp")]
    paths += [Path(__file__).with_name("grammar.py")]
    return tree_hash(paths, ROOT)


def prepare(args) -> int:
    start = time.monotonic()
    folder = args.artifacts.resolve()
    folder.mkdir(parents=True, exist_ok=True)
    manifest_path = folder / "manifest.json"
    source = args.asgp_source.resolve()
    identity = {"contract": CONTRACT, "asgp_source_sha256": asgp_source_hash(source),
                "adapter_sha256": adapter_hash(), "population_seed": 0, "data_seed": 20260626}
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {**identity, "tasks": {}}
    if any(manifest.get(k) != v for k, v in identity.items()):
        raise ValueError("artifact identity changed; prepare in a new directory")
    count = 8192 if args.suite == "scaling" else 1024
    for name in TASKS:
        grammar = folder / f"{name}.grammar.json"
        source_path = folder / f"{name}.source.json"
        prepared = folder / f"{name}.p{count}.prepared.json"
        text = json.dumps(definition(ROOT, name), separators=(",", ":")) + "\n"
        if grammar.exists() and grammar.read_text() != text:
            raise ValueError(f"grammar identity changed: {name}")
        grammar.write_text(text)
        records = manifest["tasks"].setdefault(name, {})
        for path in (source_path, prepared):
            if path.name in records and (not path.exists() or sha256(path) != records[path.name]):
                raise ValueError(f"artifact changed: {path}")
        commands = []
        if source_path.name not in records:
            if source_path.exists():
                raise ValueError(f"unregistered artifact: {source_path}")
            commands.append(([str(args.binary), "freeze", name, str(source_path)], source_path))
        if prepared.name not in records:
            if prepared.exists():
                raise ValueError(f"unregistered artifact: {prepared}")
            commands.append(([str(args.binary), "prepare", str(source_path), str(grammar), str(count), str(prepared)], prepared))
        for command, path in commands:
            print(f"prepare {name}: {command[1]}", flush=True)
            result = invoke(command, folder / f"{path.name}.log")
            if result["status"] != "ok":
                raise RuntimeError(f"preparation failed: {result}")
            records[path.name] = sha256(path)
            records[grammar.name] = sha256(grammar)
            save(manifest_path, manifest)
        audit = folder / f"{name}.audit.json"
        result = invoke([str(args.binary), "audit", str(source_path), str(grammar), str(audit)],
                        folder / f"{name}.audit.log")
        if result["status"] != "ok":
            raise RuntimeError(f"known-solution audit failed: {result}")
        records[audit.name] = sha256(audit)
        save(manifest_path, manifest)
    manifest["last_prepare_seconds"] = time.monotonic() - start
    manifest["preparation_binary_sha256"] = sha256(args.binary)
    save(manifest_path, manifest)
    print(f"Artifacts ready: {folder} ({manifest['last_prepare_seconds']:.1f} s)", flush=True)
    return 0


def environment(binary: Path) -> dict:
    cache = binary.parent / "CMakeCache.txt"
    flags = [line for line in cache.read_text().splitlines()
             if line.startswith(("CMAKE_BUILD_TYPE:", "CMAKE_CXX_FLAGS", "CMAKE_CUDA_FLAGS",
                                 "CMAKE_CUDA_ARCHITECTURES:", "GAGP_ASGP_SOURCE_DIR:"))] if cache.exists() else []
    return {"binary": str(binary), "binary_sha256": sha256(binary), "build_settings": flags,
            "runner_sha256": sha256(Path(__file__)),
            "git_commit": capture(["git", "rev-parse", "HEAD"]),
            "git_status": capture(["git", "status", "--short"]),
            "cpp_source_sha256": tree_hash([p for p in (ROOT / "cpp").rglob("*")
                                            if p.is_file() and "build" not in p.parts and
                                            "build" not in str(p.relative_to(ROOT / "cpp")).split('/')[0]], ROOT),
            "platform": platform.platform(), "cpu": capture(["lscpu"]),
            "gpu": capture(["nvidia-smi", "--query-gpu=index,name,driver_version,memory.used,utilization.gpu", "--format=csv"]),
            "cuda_device": os.environ.get("GAGP_CUDA_DEVICE", "automatic"),
            "allocator_preload": os.environ.get("LD_PRELOAD", "")}


def report(folder: Path, manifest: dict) -> None:
    rows = summarize(manifest["cells"])
    summary = {"contract": CONTRACT, "suite": manifest["suite"], "complete": manifest["complete"],
               "wall_seconds": manifest["wall_seconds"],
               "daily_budget_met": manifest["wall_seconds"] <= 300 if manifest["suite"] == "daily" else None,
               "rows": rows}
    save(folder / "summary.json", summary)
    fields = ["task", "population", "mode", "status", "median_ms", "min_ms", "max_ms",
              "speedup_asgp_1t", "speedup_gagp_cpu", "overlap_speedup"]
    with (folder / "summary.csv").open("w") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, extrasaction="ignore")
        writer.writeheader(); writer.writerows(rows)
    lines = ["# Fixed ASGP/GAGP benchmark", "", f"Contract: `{CONTRACT}`; suite: `{manifest['suite']}`.",
             f"Complete: {manifest['complete']}; process wall time: {manifest['wall_seconds']:.2f} s.", "",
             "One warmup and three measured fixed-parent generations; 1024 cases per program.",
             "Times include generation compilation, evaluation and reproduction. Setup is outside generation timing but inside process wall time.",
             "ASGP/GAGP numerical and reproduction-policy differences are accepted. This is not time-to-solution.", "",
             "| Task | P | Mode | Median ms | Range ms | vs ASGP 1T | vs GAGP CPU |", "|---|---:|---|---:|---:|---:|---:|"]
    for row in rows:
        if row["status"] != "ok":
            lines.append(f"| {row['task']} | {row['population']} | {row['mode']} | {row['status']} | — | — | — |")
        else:
            ratios = [f"{row[k]:.3f}×" if k in row else "—" for k in ("speedup_asgp_1t", "speedup_gagp_cpu")]
            lines.append(f"| {row['task']} | {row['population']} | {row['mode']} | {row['median_ms']:.3f} | "
                         f"{row['min_ms']:.3f}–{row['max_ms']:.3f} | {ratios[0]} | {ratios[1]} |")
    lines += ["", "Full commands, samples, status and artifact identities: [manifest.json](manifest.json).",
              "Hardware/build: [environment.json](environment.json). Phase attribution: [summary.json](summary.json).",
              "", "Variation counters classify operator outputs, not final offspring. Native fitness APIs do not expose per-case error/timeout counts; these are not inferred from fitness penalties."]
    (folder / "report.md").write_text("\n".join(lines) + "\n")


def run(args) -> int:
    start = time.monotonic()
    artifacts = args.artifacts.resolve()
    frozen = json.loads((artifacts / "manifest.json").read_text())
    if frozen["contract"] != CONTRACT or frozen["adapter_sha256"] != adapter_hash():
        raise ValueError("incompatible artifact contract/adapter")
    if frozen["asgp_source_sha256"] != asgp_source_hash(args.asgp_source.resolve()):
        raise ValueError("ASGP baseline source changed; rebuild and prepare new artifacts")
    folder = args.out.resolve()
    folder.mkdir(parents=True, exist_ok=False)
    save(folder / "environment.json", environment(args.binary))
    populations = POPULATIONS if args.suite == "scaling" else (1024,)
    manifest = {"contract": CONTRACT, "suite": args.suite, "artifacts": str(artifacts),
                "artifact_manifest_sha256": sha256(artifacts / "manifest.json"),
                "frozen": frozen, "cells": [], "complete": False, "wall_seconds": 0.0}
    save(folder / "manifest.json", manifest)
    for task_index, name in enumerate(TASKS):
        files = frozen["tasks"][name]
        for filename, digest in files.items():
            if sha256(artifacts / filename) != digest:
                raise ValueError(f"artifact hash mismatch: {filename}")
        for population in populations:
            count = 8192 if args.suite == "scaling" else 1024
            prepared = artifacts / f"{name}.p{count}.prepared.json"
            if prepared.name not in files:
                raise ValueError(f"run prepare --suite {args.suite} first")
            # Rotate cell order across tasks; each isolated process warms its own mode.
            modes = MODES[task_index:] + MODES[:task_index]
            for mode in modes:
                stem = f"{name}.p{population}.{mode}"
                output = folder / f"{stem}.jsonl"
                command = [str(args.binary), "measure", str(artifacts / f"{name}.source.json"),
                           str(prepared), str(artifacts / f"{name}.grammar.json"), str(population), mode, str(output)]
                cell = {"task": name, "population": population, "mode": mode,
                        **invoke(command, folder / f"{stem}.log", args.cell_timeout)}
                cell["rows"] = []
                if output.exists():
                    cell["raw_sha256"] = sha256(output)
                    try:
                        cell["rows"] = [json.loads(line) for line in output.read_text().splitlines()]
                        if cell["status"] == "ok":
                            validate_rows(cell["rows"], name, population, mode)
                    except (ValueError, KeyError) as error:
                        cell.update(status="invalid", error=str(error))
                elif cell["status"] == "ok":
                    cell["status"] = "missing"
                manifest["cells"].append(cell)
                manifest["wall_seconds"] = time.monotonic() - start
                save(folder / "manifest.json", manifest)
                detail = ""
                if cell["status"] == "ok":
                    detail = f" {statistics.median(r['generation_ms'] for r in cell['rows'][1:]):.3f} ms"
                print(f"{name} P={population} {mode}: {cell['status']}{detail} "
                      f"(elapsed {manifest['wall_seconds']:.1f}s)", flush=True)
    manifest["complete"] = all(c["status"] == "ok" for c in manifest["cells"])
    manifest["wall_seconds"] = time.monotonic() - start
    save(folder / "manifest.json", manifest)
    report(folder, manifest)
    print(f"Report: {folder / 'report.md'}", flush=True)
    return 0 if manifest["complete"] else 1


def main() -> int:
    parser = argparse.ArgumentParser(description="Fixed three-task ASGP/GAGP comparison; see docs/guides/fixed-asgp-benchmark.md")
    parser.add_argument("action", choices=("prepare", "run"))
    parser.add_argument("--binary", type=Path, default=ROOT / "cpp/build/release/gagp_fixed_asgp_bench")
    parser.add_argument("--artifacts", type=Path, default=ROOT / "logs/fixed-asgp/artifacts")
    parser.add_argument("--asgp-source", type=Path, default=Path.home() / "r13921069")
    parser.add_argument("--suite", choices=("daily", "scaling"), default="daily")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--cell-timeout", type=float, help="seconds per process (daily: 300, scaling: 1800)")
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    if args.cell_timeout is None:
        args.cell_timeout = 1800 if args.suite == "scaling" else 300
    if not math.isfinite(args.cell_timeout) or args.cell_timeout <= 0:
        parser.error("--cell-timeout must be finite and positive")
    if args.action == "run" and args.out is None:
        parser.error("run requires a new --out directory")
    return prepare(args) if args.action == "prepare" else run(args)


if __name__ == "__main__":
    raise SystemExit(main())
