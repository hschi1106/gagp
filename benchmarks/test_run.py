"""Tests of benchmarks/run.py against a stand-in `gagp` executable."""
from __future__ import annotations

import contextlib
import io
import json
import os
import stat
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run  # noqa: E402

# Writes a gagp-run-v1 result. Task "broken" fails; the engine scales the time;
# FAKE_DIVERGE makes the CPU engine find a different program.
FAKE_GAGP = r'''#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
args = sys.argv[2:]
task = Path(args[0]).parent.name
opt = {args[i]: args[i + 1] for i in range(1, len(args) - 1, 2) if args[i].startswith("--")}
with open(os.environ["FAKE_CALLS"], "a") as calls:
    calls.write(task + "\n")
if task == "broken":
    sys.exit("gagp run: boom")
engine, seed = opt["--engine"], int(opt["--seed"])
program = {"op": "x"} if engine == "gpu" or not os.environ.get("FAKE_DIVERGE") else {"op": "y"}
out = Path(opt["--out"]); out.mkdir(parents=True, exist_ok=True)
(out / "result.json").write_text(json.dumps({
    "format": "gagp-run-v1", "best": {"wrong": 0 if seed == 1 else 3, "program": program},
    "test": {"cases": 10, "correct": 10 if seed == 1 else 4, "solved": seed == 1},
    "history": [{"best_fitness": -1.0}, {"best_fitness": 0.0}],
    "time_ms": {"total": 1000.0 if engine == "gpu" else 50000.0}}))
'''


class BenchRunTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.suite, self.out = root / "suite", root / "out"
        for task in ("alpha", "beta", "broken"):
            (self.suite / task).mkdir(parents=True)
            (self.suite / task / "task.json").write_text("{}")
        (self.suite / "asgp_paper.json").write_text(json.dumps({"tasks": [
            {"task": "alpha", "scheme": "divide_and_conquer", "paper_solved_of_100": 97},
            {"task": "beta", "scheme": "dynamic_programming", "paper_solved_of_100": 23, "thesis_solved_of_25": 23}]}))
        self.gagp = root / "gagp"
        self.gagp.write_text(FAKE_GAGP)
        self.gagp.chmod(self.gagp.stat().st_mode | stat.S_IEXEC)
        self.calls = root / "calls.txt"
        os.environ["FAKE_CALLS"] = str(self.calls)
        os.environ.pop("FAKE_DIVERGE", None)

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def bench(self, *extra: str) -> tuple[int, str]:
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = run.main(["run", "--suite", str(self.suite), "--out", str(self.out), "--gagp", str(self.gagp),
                             "--population", "64", "--generations", "2", *extra])
        return code, output.getvalue()

    def test_parse_seeds(self) -> None:
        self.assertEqual(run.parse_seeds("1-3,7"), [1, 2, 3, 7])
        with self.assertRaises(Exception):
            run.parse_seeds("x")

    def test_runs_resume_and_report(self) -> None:
        code, _ = self.bench("--tasks", "alpha,beta", "--seeds", "1-2")
        self.assertEqual(code, 0)
        self.assertEqual(len(self.calls.read_text().split()), 4)
        manifest = json.loads((self.out / "gpu" / "p64-g2" / "run.json").read_text())
        self.assertEqual((manifest["engine"], manifest["population"]), ("gpu", 64))
        # A second invocation finds every run done and starts nothing.
        code, printed = self.bench("--tasks", "alpha,beta", "--seeds", "1-2")
        self.assertEqual((code, len(self.calls.read_text().split())), (0, 4))
        self.assertIn("4 already done", printed)
        report = (self.out / "report.md").read_text()
        self.assertIn("| alpha | DC | 1/2 | 1/2 | 1.00 | 97 |  |", report)
        self.assertIn("| beta | DP | 1/2 | 1/2 | 1.00 | 23 | 23 |", report)
        self.assertIn("**2/4**", report)

    def test_failure_is_reported_and_retried(self) -> None:
        code, printed = self.bench("--tasks", "broken,alpha", "--seeds", "1")
        self.assertEqual(code, 1)
        self.assertIn("broken s1: FAILED", printed)
        self.assertIn("boom", (self.out / "gpu" / "p64-g2" / "broken" / "s1" / "error.txt").read_text())
        self.bench("--tasks", "broken,alpha", "--seeds", "1")
        self.assertEqual(self.calls.read_text().split(), ["broken", "alpha", "broken"])

    def test_engine_comparison(self) -> None:
        self.bench("--tasks", "alpha", "--seeds", "1-2")
        self.bench("--tasks", "alpha", "--seeds", "1-2", "--engine", "cpu", "--jobs", "2")
        report = (self.out / "report.md").read_text()
        self.assertIn("| alpha | 2 | 100.0 | 2.00 | 50.0x | yes |", report)
        os.environ["FAKE_DIVERGE"] = "1"
        for path in (self.out / "cpu").glob("*/*/s*/result.json"):
            path.unlink()
        self.bench("--tasks", "alpha", "--seeds", "1-2", "--engine", "cpu")
        self.assertIn("| alpha | 2 | 100.0 | 2.00 | 50.0x | **NO** |", (self.out / "report.md").read_text())

    def test_rejects_unknown_tasks_and_parallel_gpu(self) -> None:
        with self.assertRaises(SystemExit) as unknown:
            self.bench("--tasks", "nope")
        self.assertIn("unknown task(s) nope", str(unknown.exception))
        with self.assertRaises(SystemExit):
            self.bench("--jobs", "2")


if __name__ == "__main__":
    unittest.main()
