#!/usr/bin/env python3
"""Run a GAGP benchmark suite and report it against the ASGP paper.

    python3 benchmarks/run.py run [--quick] [--engine gpu|cpu] [options]
    python3 benchmarks/run.py report [OUT]

`run` executes `gagp run` once per task and seed and writes
OUT/<engine>/p<P>-g<G>/<task>/s<seed>/result.json. Finished runs are skipped,
so an interrupted benchmark resumes where it stopped. `report` summarizes every
configuration found under OUT into OUT/report.md. See benchmarks/README.md.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import shutil
import statistics
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SUITE = ROOT / "benchmarks" / "asgp30"
OUT = ROOT / "logs" / "bench" / "asgp30"
GAGP = ROOT / "cpp" / "build" / "gagp"
# --quick: two divide-and-conquer list tasks, a string task and two DP tasks.
QUICK_TASKS = ["count_odds", "median", "smallest", "double_letters", "fibonacci", "house_robber"]
QUICK_GENERATIONS = 16


def parse_seeds(text: str) -> list[int]:
    seeds: list[int] = []
    for part in text.split(","):
        first, _, last = part.strip().partition("-")
        try:
            seeds.extend(range(int(first), int(last or first) + 1))
        except ValueError:
            raise argparse.ArgumentTypeError(f"bad seed list {text!r}; use e.g. 1-5 or 1,3,7") from None
    if not seeds or min(seeds) < 0:
        raise argparse.ArgumentTypeError("seeds must be nonnegative")
    return seeds


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_result(path: Path) -> dict | None:
    try:
        result = json.loads(path.read_text())
    except (OSError, ValueError):
        return None
    return result if result.get("format") == "gagp-run-v1" else None


def machine() -> dict:
    info = {"host": platform.node(), "cpu": platform.processor() or platform.machine()}
    try:
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                info["cpu"] = line.split(":", 1)[1].strip()
                break
    except OSError:
        pass
    if shutil.which("nvidia-smi"):
        query = subprocess.run(["nvidia-smi", "--query-gpu=name", "--format=csv,noheader"],
                               capture_output=True, text=True)
        if query.returncode == 0:
            info["gpus"] = query.stdout.split("\n")[0].strip()
    return info


def git_revision() -> str:
    query = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"], capture_output=True, text=True)
    return query.stdout.strip() if query.returncode == 0 else "unknown"


def command_run(args: argparse.Namespace) -> int:
    if args.quick:
        args.tasks = args.tasks or ",".join(QUICK_TASKS)
        args.seeds = args.seeds or [1]
        args.generations = args.generations or QUICK_GENERATIONS
    seeds = args.seeds or list(range(1, 6))
    generations = args.generations or 64
    available = sorted(p.name for p in args.suite.iterdir() if (p / "task.json").exists())
    tasks = args.tasks.split(",") if args.tasks else available
    unknown = sorted(set(tasks) - set(available))
    if unknown:
        sys.exit(f"unknown task(s) {', '.join(unknown)}; the suite has: {', '.join(available)}")
    if not args.gagp.exists():
        sys.exit(f"{args.gagp} not found; build it first (see README.md) or pass --gagp")
    if args.jobs > 1 and args.engine == "gpu":
        sys.exit("--jobs applies to --engine cpu; GPU runs share one device and run one at a time")

    config = args.out / args.engine / f"p{args.population}-g{generations}"
    config.mkdir(parents=True, exist_ok=True)
    manifest = {"format": "gagp-bench-run-v1", "suite": str(args.suite), "engine": args.engine,
                "population": args.population, "generations": generations,
                "search": json.loads(args.search.read_text()) if args.search else "built-in default",
                "gagp_sha256": sha256(args.gagp), "git": git_revision(), "machine": machine()}
    (config / "run.json").write_text(json.dumps(manifest, indent=1) + "\n")

    jobs = [(t, s) for t in tasks for s in seeds]
    done = sum(load_result(config / t / f"s{s}" / "result.json") is not None for t, s in jobs)
    print(f"{len(jobs)} runs ({len(tasks)} tasks x {len(seeds)} seeds), {done} already done; "
          f"P{args.population} G{generations} on {args.engine}; output {config}", flush=True)
    counter = iter(range(done + 1, len(jobs) + 1))

    def one(job: tuple[str, int]) -> bool:
        task, seed = job
        out = config / task / f"s{seed}"
        if load_result(out / "result.json") is not None:
            return True
        command = [str(args.gagp), "run", str(args.suite / task / "task.json"), "--seed", str(seed),
                   "--population", str(args.population), "--generations", str(generations),
                   "--engine", args.engine, "--out", str(out), "--quiet"]
        if args.search:
            command += ["--search", str(args.search)]
        out.mkdir(parents=True, exist_ok=True)
        started = time.monotonic()
        finished = subprocess.run(command, capture_output=True, text=True)
        seconds = time.monotonic() - started
        result = load_result(out / "result.json")
        if finished.returncode or result is None:
            (out / "error.txt").write_text(" ".join(command) + "\n\n" + finished.stderr)
            print(f"[{next(counter)}/{len(jobs)}] {task} s{seed}: FAILED (exit {finished.returncode}), "
                  f"see {out / 'error.txt'}", flush=True)
            return False
        test = result.get("test", {})
        verdict = "solved" if test.get("solved") else f"test {test.get('correct', 0):g}/{test.get('cases', 0):g}"
        print(f"[{next(counter)}/{len(jobs)}] {task} s{seed}: {verdict}, {seconds:.1f} s", flush=True)
        return True

    with ThreadPoolExecutor(args.jobs) as pool:
        ok = all(list(pool.map(one, jobs)))
    report = write_report(args.out, args.suite)
    print(f"report: {report}")
    return 0 if ok else 1


def geometric_mean(values: list[float]) -> float:
    return math.exp(sum(math.log(v) for v in values) / len(values)) if values else float("nan")


def collect(out: Path) -> dict[tuple[str, str], dict[str, dict[int, dict]]]:
    """(engine, config) -> task -> seed -> result."""
    found: dict[tuple[str, str], dict[str, dict[int, dict]]] = {}
    for path in sorted(out.glob("*/*/*/s*/result.json")):
        seed_dir = path.parent
        task_dir, config_dir = seed_dir.parent, seed_dir.parent.parent
        result = load_result(path)
        if result is None:
            continue
        key = (config_dir.parent.name, config_dir.name)
        found.setdefault(key, {}).setdefault(task_dir.name, {})[int(seed_dir.name[1:])] = result
    return found


def summary_table(runs: dict[str, dict[int, dict]], paper: dict[str, dict]) -> list[str]:
    lines = ["| Task | Scheme | GAGP solved | Train perfect | Median time (s) | ASGP paper /100 | ASGP thesis /25 |",
             "| --- | --- | ---: | ---: | ---: | ---: | ---: |"]
    solved_total = runs_total = 0
    seconds_total = 0.0
    for task in sorted(runs, key=lambda t: (paper.get(t, {}).get("scheme", ""), t)):
        results = list(runs[task].values())
        solved = sum(bool(r.get("test", {}).get("solved")) for r in results)
        perfect = sum(r["best"]["wrong"] == 0 for r in results)
        seconds = [r["time_ms"]["total"] / 1000 for r in results]
        reference = paper.get(task, {})
        scheme = {"divide_and_conquer": "DC", "dynamic_programming": "DP"}.get(reference.get("scheme"), "")
        thesis = reference.get("thesis_solved_of_25")
        lines.append(f"| {task} | {scheme} | {solved}/{len(results)} | {perfect}/{len(results)} | "
                     f"{statistics.median(seconds):.2f} | {reference.get('paper_solved_of_100', '')} | "
                     f"{'' if thesis is None else thesis} |")
        solved_total += solved
        runs_total += len(results)
        seconds_total += sum(seconds)
    lines.append(f"| **all {len(runs)} tasks** | | **{solved_total}/{runs_total}** | | "
                 f"total {seconds_total:.0f} s | | |")
    return lines


def comparison(gpu: dict[str, dict[int, dict]], cpu: dict[str, dict[int, dict]]) -> list[str]:
    lines = ["| Task | Runs | CPU time (s) | GPU time (s) | Speedup | Same result |",
             "| --- | ---: | ---: | ---: | ---: | --- |"]
    ratios: list[float] = []
    same_all = True
    cpu_sum = gpu_sum = 0.0
    for task in sorted(set(gpu) & set(cpu)):
        seeds = sorted(set(gpu[task]) & set(cpu[task]))
        if not seeds:
            continue
        c = sum(cpu[task][s]["time_ms"]["total"] for s in seeds) / 1000
        g = sum(gpu[task][s]["time_ms"]["total"] for s in seeds) / 1000
        # The engine never changes the search: the best program and the per-generation
        # fitness history must match exactly.
        same = all(gpu[task][s]["best"]["program"] == cpu[task][s]["best"]["program"] and
                   [h["best_fitness"] for h in gpu[task][s]["history"]] ==
                   [h["best_fitness"] for h in cpu[task][s]["history"]] for s in seeds)
        same_all &= same
        ratios.append(c / g)
        cpu_sum += c
        gpu_sum += g
        lines.append(f"| {task} | {len(seeds)} | {c:.1f} | {g:.2f} | {c / g:.1f}x | {'yes' if same else '**NO**'} |")
    if not ratios:
        return []
    lines.append(f"| **all** | | {cpu_sum:.0f} | {gpu_sum:.1f} | geometric mean {geometric_mean(ratios):.1f}x, "
                 f"total {cpu_sum / gpu_sum:.1f}x | {'yes' if same_all else '**NO**'} |")
    return lines


def write_report(out: Path, suite: Path) -> Path:
    paper_path = suite / "asgp_paper.json"
    paper = {row["task"]: row for row in json.loads(paper_path.read_text())["tasks"]} if paper_path.exists() else {}
    found = collect(out)
    lines = [f"# Benchmark report: {suite.name}", "",
             "A run is *solved* when its best program answers every held-out test case. "
             "*Train perfect* counts runs whose best program has no wrong training case. "
             "The ASGP columns are the published success counts (paper: 100 runs per task; "
             "thesis: 25 runs per DP task, the same counts). They used a different data set and "
             "budget, so they are a reference, not a matched baseline (benchmarks/README.md).", ""]
    for (engine, config), runs in sorted(found.items()):
        manifest_path = out / engine / config / "run.json"
        manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
        where = manifest.get("machine", {})
        lines += [f"## {engine.upper()}, {config.replace('p', 'population ', 1).replace('-g', ', generations ')}", "",
                  f"gagp `{manifest.get('gagp_sha256', '?')[:12]}`, git `{manifest.get('git', '?')[:12]}`, "
                  f"{where.get('gpus', '') if engine == 'gpu' else where.get('cpu', '')}", ""]
        lines += summary_table(runs, paper) + [""]
    for (engine, config), runs in sorted(found.items()):
        if engine == "gpu" and ("cpu", config) in found:
            table = comparison(runs, found[("cpu", config)])
            if table:
                lines += [f"## GPU engine vs CPU reference engine ({config})", "",
                          "Both engines run the same search; *Same result* checks that every common run "
                          "found the same best program with the same fitness history. Times are wall-clock "
                          "seconds of whole runs, CPU on one thread.", ""] + table + [""]
    report = out / "report.md"
    out.mkdir(parents=True, exist_ok=True)
    report.write_text("\n".join(lines))
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    run = commands.add_parser("run", help="run the suite (resumes an interrupted run)")
    run.add_argument("--quick", action="store_true",
                     help=f"smoke test: {len(QUICK_TASKS)} tasks, seed 1, {QUICK_GENERATIONS} generations")
    run.add_argument("--engine", choices=["gpu", "cpu"], default="gpu")
    run.add_argument("--tasks", help="comma-separated task names (default: all)")
    run.add_argument("--seeds", type=parse_seeds, help="e.g. 1-5 (default) or 1,3")
    run.add_argument("--population", type=int, default=1024)
    run.add_argument("--generations", type=int, help="default 64")
    run.add_argument("--search", type=Path, help="search configuration (default: gagp's built-in default)")
    run.add_argument("--jobs", type=int, default=1, help="parallel CPU runs (cpu engine only)")
    run.add_argument("--suite", type=Path, default=SUITE)
    run.add_argument("--out", type=Path, default=OUT)
    run.add_argument("--gagp", type=Path, default=GAGP)
    report = commands.add_parser("report", help="write OUT/report.md from finished runs")
    report.add_argument("out", nargs="?", type=Path, default=OUT)
    report.add_argument("--suite", type=Path, default=SUITE)
    args = parser.parse_args(argv)
    if args.command == "run":
        args.suite, args.out, args.gagp = args.suite.resolve(), args.out.resolve(), args.gagp.resolve()
        if args.population < 2 or args.jobs < 1:
            parser.error("--population must be at least 2 and --jobs at least 1")
        return command_run(args)
    print(write_report(args.out, args.suite).read_text())
    return 0


if __name__ == "__main__":
    sys.exit(main())
