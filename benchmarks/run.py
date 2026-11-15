#!/usr/bin/env python3
"""Run a GAGP benchmark suite and report it against published results.

    python3 benchmarks/run.py run [--suite asgp30|psb1_g3p] [--quick] [--engine gpu|cpu] [options]
    python3 benchmarks/run.py report [OUT] [--suite asgp30|psb1_g3p]

`run` executes `gagp run` once per task and seed and writes
OUT/<engine>/<configuration>/<task>/s<seed>/result.json, where the configuration
is p<P>-g<G> for asgp30 and `thesis` for psb1_g3p. Finished runs are skipped,
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
from collections.abc import Callable
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SUITES = {"asgp30": ROOT / "benchmarks" / "asgp30", "psb1_g3p": ROOT / "benchmarks" / "psb1_g3p"}
SUITE = SUITES["asgp30"]
GAGP = ROOT / "cpp" / "build" / "gagp"
# --quick: two divide-and-conquer list tasks, a string task and two DP tasks.
QUICK_TASKS = ["count_odds", "median", "smallest", "double_letters", "fibonacci", "house_robber"]
QUICK_GENERATIONS = 16
# A suite with a `literature.json` (psb1_g3p) takes its population, generations and
# search settings from each task's own search.json, runs seeds 1-100 and, with --quick,
# every task for 10 generations.
G3P_SEEDS = list(range(1, 101))
G3P_QUICK_GENERATIONS = 10


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


def suite_arg(text: str) -> Path:
    """A suite name from SUITES or a directory."""
    return SUITES.get(text) or Path(text)


def is_g3p(suite: Path) -> bool:
    return (suite / "literature.json").exists()


def default_out(suite: Path) -> Path:
    return ROOT / "logs" / "bench" / suite.name


def config_name(g3p: bool, population: int | None, generations: int | None, search: Path | None) -> str:
    if not g3p:
        return f"p{population}-g{generations}"
    name = "thesis" if search is None else f"search-{search.stem}"
    return name + (f"-p{population}" if population else "") + (f"-g{generations}" if generations else "")


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
    g3p = is_g3p(args.suite)
    if args.quick:
        args.tasks = args.tasks or (None if g3p else ",".join(QUICK_TASKS))
        args.seeds = args.seeds or [1]
        args.generations = args.generations or (G3P_QUICK_GENERATIONS if g3p else QUICK_GENERATIONS)
    if g3p:  # population and generations come from each task's search.json unless given
        seeds, population, generations = args.seeds or G3P_SEEDS, args.population, args.generations
    else:
        seeds, population, generations = args.seeds or list(range(1, 6)), args.population or 1024, args.generations or 64
    available = sorted(p.name for p in args.suite.iterdir() if (p / "task.json").exists())
    tasks = args.tasks.split(",") if args.tasks else available
    unknown = sorted(set(tasks) - set(available))
    if unknown:
        sys.exit(f"unknown task(s) {', '.join(unknown)}; the suite has: {', '.join(available)}")
    if not args.gagp.exists():
        sys.exit(f"{args.gagp} not found; build it first (see README.md) or pass --gagp")
    if args.jobs > 1 and args.engine == "gpu":
        sys.exit("--jobs applies to --engine cpu; GPU runs share one device and run one at a time")

    if g3p:
        missing = [t for t in tasks if not (args.suite / t / "train.cases.json").exists()]
        if missing:
            sys.exit(f"no case files for {', '.join(missing)}; create them with "
                     f"python3 {args.suite / 'build.py'} cases --data PONYGE2_CHECKOUT")
        searches = {t: args.search or args.suite / t / "search.json" for t in tasks}
    else:
        searches = {t: args.search for t in tasks}
    config = args.out / args.engine / config_name(g3p, population, generations, args.search)
    config.mkdir(parents=True, exist_ok=True)
    default_search = "each task's search.json" if g3p else "built-in default"
    manifest = {"format": "gagp-bench-run-v1", "suite": str(args.suite), "engine": args.engine,
                "population": population or "from search.json", "generations": generations or "from search.json",
                "search": json.loads(args.search.read_text()) if args.search else default_search,
                "gagp_sha256": sha256(args.gagp), "git": git_revision(), "machine": machine()}
    (config / "run.json").write_text(json.dumps(manifest, indent=1) + "\n")

    jobs = [(t, s) for s in seeds for t in tasks] if g3p else [(t, s) for t in tasks for s in seeds]
    done = sum(load_result(config / t / f"s{s}" / "result.json") is not None for t, s in jobs)
    settings = f"P{population} G{generations}"
    if g3p:
        given = [f"{n}{v}" for n, v in (("P", population), ("G", generations)) if v]
        settings = " ".join(given) + (" and " if given else "") + "the rest from each search.json"
    print(f"{len(jobs)} runs ({len(tasks)} tasks x {len(seeds)} seeds), {done} already done; "
          f"{settings} on {args.engine}; output {config}", flush=True)
    counter = iter(range(done + 1, len(jobs) + 1))

    def one(job: tuple[str, int]) -> bool:
        task, seed = job
        out = config / task / f"s{seed}"
        if load_result(out / "result.json") is not None:
            return True
        command = [str(args.gagp), "run", str(args.suite / task / "task.json"), "--seed", str(seed)]
        if population:
            command += ["--population", str(population)]
        if generations:
            command += ["--generations", str(generations)]
        command += ["--engine", args.engine, "--out", str(out)]
        if searches[task]:
            command += ["--search", str(searches[task])]
        command.append("--quiet")
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


def same_program_and_history(a: dict, b: dict) -> bool:
    """The engine never changes the search: the best program and the per-generation fitness
    history must match exactly."""
    return (a["best"]["program"] == b["best"]["program"] and
            [h["best_fitness"] for h in a["history"]] == [h["best_fitness"] for h in b["history"]])


def without_timing(value: object) -> object:
    """A result with its timing (`*_ms` members, `time_ms`) and the engine name removed."""
    if isinstance(value, dict):
        return {k: without_timing(v) for k, v in value.items()
                if not (k.endswith("_ms") or k in ("time_ms", "engine"))}
    if isinstance(value, list):
        return [without_timing(v) for v in value]
    return value


def identical_results(a: dict, b: dict) -> bool:
    """Everything but timing and the engine name is equal: champion, fitness, test verdict, history."""
    return without_timing(a) == without_timing(b)


def comparison(gpu: dict[str, dict[int, dict]], cpu: dict[str, dict[int, dict]],
               same_result: Callable[[dict, dict], bool] = same_program_and_history) -> list[str]:
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
        same = all(same_result(gpu[task][s], cpu[task][s]) for s in seeds)
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


def g3p_table(runs: dict[str, dict[int, dict]], literature: dict) -> list[str]:
    sources = literature.get("sources", [])
    counts = literature.get("solved_of_runs", {})
    lines = ["| Task | Runs | GAGP solved | Train perfect | Median time (s) | "
             + " | ".join(f"{s['label']} /{s['runs']}" for s in sources) + " |",
             "| --- | ---: | ---: | ---: | ---: |" + " ---: |" * len(sources)]
    solved_total = runs_total = 0
    seconds_total = 0.0
    for task in sorted(runs, key=lambda t: (list(counts).index(t) if t in counts else len(counts), t)):
        results = list(runs[task].values())
        solved = sum(bool(r.get("test", {}).get("solved")) for r in results)
        perfect = sum(r["best"]["wrong"] == 0 for r in results)
        seconds = [r["time_ms"]["total"] / 1000 for r in results]
        published = " | ".join(str(counts.get(task, {}).get(s["id"], "")) for s in sources)
        lines.append(f"| {task} | {len(results)} | {solved}/{len(results)} | {perfect}/{len(results)} | "
                     f"{statistics.median(seconds):.2f} | {published} |")
        solved_total += solved
        runs_total += len(results)
        seconds_total += sum(seconds)
    lines.append(f"| **all {len(runs)} tasks** | {runs_total} | **{solved_total}/{runs_total}** | | "
                 f"total {seconds_total:.0f} s |{' |' * len(sources)}")
    return lines


def write_g3p_report(out: Path, suite: Path) -> Path:
    literature = json.loads((suite / "literature.json").read_text())
    found = collect(out)
    lines = [f"# Benchmark report: {suite.name}", "",
             "A run is *solved* when its best program answers every held-out test case. "
             "*Train perfect* counts runs whose best program has no wrong training case. "
             "The published columns are the runs out of 100 that solved the problem in the G3P "
             "work; each column header names its source (`literature.json`). Those runs used a "
             "different initialization, node limit and execution limit, so the counts are a "
             "reference, not a matched baseline (benchmarks/psb1_g3p/README.md).", ""]
    for (engine, config), runs in sorted(found.items()):
        manifest_path = out / engine / config / "run.json"
        manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
        where = manifest.get("machine", {})
        lines += [f"## {engine.upper()}, {config}", "",
                  f"population {manifest.get('population', '?')}, generations {manifest.get('generations', '?')}; "
                  f"gagp `{manifest.get('gagp_sha256', '?')[:12]}`, git `{manifest.get('git', '?')[:12]}`, "
                  f"{where.get('gpus', '') if engine == 'gpu' else where.get('cpu', '')}", ""]
        lines += g3p_table(runs, literature) + [""]
    for (engine, config), runs in sorted(found.items()):
        if engine == "gpu" and ("cpu", config) in found:
            table = comparison(runs, found[("cpu", config)], identical_results)
            if table:
                lines += [f"## GPU engine vs CPU reference engine ({config})", "",
                          "Both engines run the same search. *Same result* checks that every common run "
                          "has an identical result file apart from its timing fields and the engine name: "
                          "champion, fitness, test verdict and the whole history. Times are wall-clock "
                          "seconds of whole runs, CPU on one thread.", ""] + table + [""]
    report = out / "report.md"
    out.mkdir(parents=True, exist_ok=True)
    report.write_text("\n".join(lines))
    return report


def write_report(out: Path, suite: Path) -> Path:
    if is_g3p(suite):
        return write_g3p_report(out, suite)
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
    run.add_argument("--seeds", type=parse_seeds, help="e.g. 1-5 (default; psb1_g3p: 1-100) or 1,3")
    run.add_argument("--population", type=int, help="default 1024 (psb1_g3p: from each task's search.json)")
    run.add_argument("--generations", type=int, help="default 64 (psb1_g3p: from each task's search.json)")
    run.add_argument("--search", type=Path, help="search configuration (default: gagp's built-in default)")
    run.add_argument("--jobs", type=int, default=1, help="parallel CPU runs (cpu engine only)")
    run.add_argument("--suite", type=suite_arg, default=SUITE, help="asgp30 (default), psb1_g3p or a directory")
    run.add_argument("--out", type=Path, help="default logs/bench/<suite>")
    run.add_argument("--gagp", type=Path, default=GAGP)
    report = commands.add_parser("report", help="write OUT/report.md from finished runs")
    report.add_argument("out", nargs="?", type=Path, help="default logs/bench/<suite>")
    report.add_argument("--suite", type=suite_arg, default=SUITE, help="asgp30 (default), psb1_g3p or a directory")
    args = parser.parse_args(argv)
    args.suite = args.suite.resolve()
    args.out = (args.out or default_out(args.suite)).resolve()
    if args.command == "run":
        args.gagp = args.gagp.resolve()
        if (args.population is not None and args.population < 2) or args.jobs < 1:
            parser.error("--population must be at least 2 and --jobs at least 1")
        return command_run(args)
    print(write_report(args.out, args.suite).read_text())
    return 0


if __name__ == "__main__":
    sys.exit(main())
