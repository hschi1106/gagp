"""Tests of the psb1_g3p suite paths of benchmarks/run.py against a stand-in `gagp`."""
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

# Records its command line and writes a gagp-run-v1 result. Seed 1 solves. FAKE_DIVERGE changes
# something other than the best program on the CPU engine. Times and the engine name always differ.
FAKE_GAGP = r'''#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
args = sys.argv[1:]
opt = {a: (args[i + 1] if i + 1 < len(args) and not args[i + 1].startswith("--") else True)
       for i, a in enumerate(args) if a.startswith("--")}
with open(os.environ["FAKE_CALLS"], "a") as calls:
    calls.write(json.dumps(args) + "\n")
engine, seed = opt["--engine"], int(opt["--seed"])
cpu = engine == "cpu"
mean = -2.0 if cpu and os.environ.get("FAKE_DIVERGE") else -1.0
out = Path(opt["--out"]); out.mkdir(parents=True, exist_ok=True)
(out / "result.json").write_text(json.dumps({
    "format": "gagp-run-v1", "engine": engine, "seed": str(seed),
    "best": {"wrong": 0 if seed == 1 else 3, "program": {"op": "x"}},
    "test": {"cases": 10, "correct": 10 if seed == 1 else 4, "solved": seed == 1},
    "history": [{"best_fitness": -1.0, "mean_fitness": mean, "elapsed_ms": 9.0 + cpu * 3, "eval_ms": 1.0 + cpu}],
    "time_ms": {"total": 50000.0 if cpu else 1000.0, "search": 7.0 + cpu}}))
'''


class G3pRunTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.suite, self.out = root / "psb1_g3p", root / "out"
        for task, generations in (("alpha", 200), ("beta", 300)):
            (self.suite / task).mkdir(parents=True)
            (self.suite / task / "task.json").write_text("{}")
            (self.suite / task / "train.cases.json").write_text("{}")
            (self.suite / task / "search.json").write_text(json.dumps({"generations": generations}))
        (self.suite / "literature.json").write_text(json.dumps({
            "sources": [{"id": "tourn", "label": "G3P tournament", "runs": 100},
                        {"id": "lex", "label": "G3P lexicase", "runs": 100}],
            "solved_of_runs": {"beta": {"tourn": 79, "lex": 63}, "alpha": {"tourn": 96, "lex": 89}}}))
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
            code = run.main(["run", "--suite", str(self.suite), "--out", str(self.out), "--gagp", str(self.gagp), *extra])
        return code, output.getvalue()

    def commands(self) -> list[list[str]]:
        return [json.loads(line) for line in self.calls.read_text().splitlines()]

    def test_suite_names_and_defaults(self) -> None:
        self.assertEqual(run.suite_arg("psb1_g3p"), run.SUITES["psb1_g3p"])
        self.assertEqual(run.suite_arg("asgp30"), run.SUITE)
        self.assertEqual(run.suite_arg("/some/dir"), Path("/some/dir"))
        self.assertTrue(run.is_g3p(run.SUITES["psb1_g3p"]))
        self.assertFalse(run.is_g3p(run.SUITES["asgp30"]))
        self.assertEqual(run.default_out(run.SUITES["psb1_g3p"]), run.ROOT / "logs" / "bench" / "psb1_g3p")

    def test_takes_settings_from_each_search_file_and_runs_seed_major(self) -> None:
        code, printed = self.bench("--seeds", "1-2")
        self.assertEqual(code, 0)
        self.assertIn("4 runs (2 tasks x 2 seeds)", printed)
        self.assertIn("the rest from each search.json", printed)
        commands = self.commands()
        self.assertEqual([(Path(c[1]).parent.name, c[c.index("--seed") + 1]) for c in commands],
                         [("alpha", "1"), ("beta", "1"), ("alpha", "2"), ("beta", "2")])
        for command in commands:
            self.assertNotIn("--population", command)
            self.assertNotIn("--generations", command)
            self.assertEqual(command[command.index("--search") + 1], str(Path(command[1]).parent / "search.json"))
            self.assertEqual(command[-1], "--quiet")
        self.assertTrue((self.out / "gpu" / "thesis" / "beta" / "s2" / "result.json").exists())
        manifest = json.loads((self.out / "gpu" / "thesis" / "run.json").read_text())
        self.assertEqual((manifest["population"], manifest["generations"]), ("from search.json", "from search.json"))

    def test_default_seeds_are_1_to_100_and_runs_resume(self) -> None:
        self.assertEqual(run.G3P_SEEDS, list(range(1, 101)))
        self.bench("--tasks", "alpha")
        self.assertEqual(len(self.commands()), 100)
        code, printed = self.bench("--tasks", "alpha")
        self.assertEqual((code, len(self.commands())), (0, 100))
        self.assertIn("100 already done", printed)

    def test_overrides_and_quick(self) -> None:
        self.bench("--quick")
        commands = self.commands()
        self.assertEqual(len(commands), 2)
        for command in commands:
            self.assertEqual(command[command.index("--generations") + 1], "10")
            self.assertEqual(command[command.index("--seed") + 1], "1")
        self.assertTrue((self.out / "gpu" / "thesis-g10" / "alpha" / "s1" / "result.json").exists())
        self.bench("--tasks", "alpha", "--seeds", "1", "--population", "64", "--generations", "2", "--engine", "cpu")
        last = self.commands()[-1]
        self.assertEqual((last[last.index("--population") + 1], last[last.index("--generations") + 1]), ("64", "2"))
        self.assertTrue((self.out / "cpu" / "thesis-p64-g2" / "alpha" / "s1" / "result.json").exists())

    def test_explicit_search_file_applies_to_every_task(self) -> None:
        search = self.suite.parent / "mine.json"
        search.write_text("{}")
        self.bench("--seeds", "1", "--search", str(search))
        self.assertTrue(all(c[c.index("--search") + 1] == str(search) for c in self.commands()))
        self.assertTrue((self.out / "gpu" / "search-mine" / "alpha" / "s1").is_dir())

    def test_missing_case_files_are_explained(self) -> None:
        (self.suite / "beta" / "train.cases.json").unlink()
        with self.assertRaises(SystemExit) as error:
            self.bench("--seeds", "1")
        self.assertIn("no case files for beta", str(error.exception))
        self.assertIn("build.py", str(error.exception))
        self.assertFalse(self.calls.exists())

    def test_report_lists_published_counts(self) -> None:
        self.bench("--seeds", "1-2")
        report = (self.out / "report.md").read_text()
        self.assertIn("| Task | Runs | GAGP solved | Train perfect | Median time (s) | "
                      "G3P tournament /100 | G3P lexicase /100 |", report)
        # Published order, not alphabetical. Seed 1 solves and seed 2 does not.
        self.assertIn("| alpha | 2 | 1/2 | 1/2 | 1.00 | 96 | 89 |", report)
        self.assertIn("| beta | 2 | 1/2 | 1/2 | 1.00 | 79 | 63 |", report)
        self.assertIn("**2/4**", report)
        self.assertNotIn("GPU engine vs CPU", report)

    def test_engine_comparison_ignores_timing_and_catches_other_differences(self) -> None:
        self.bench("--seeds", "1-2")
        self.bench("--seeds", "1-2", "--engine", "cpu", "--jobs", "2")
        report = (self.out / "report.md").read_text()
        self.assertIn("| alpha | 2 | 100.0 | 2.00 | 50.0x | yes |", report)
        os.environ["FAKE_DIVERGE"] = "1"
        for path in (self.out / "cpu").glob("*/*/s*/result.json"):
            path.unlink()
        self.bench("--seeds", "1-2", "--engine", "cpu")
        report = (self.out / "report.md").read_text()
        self.assertIn("| alpha | 2 | 100.0 | 2.00 | 50.0x | **NO** |", report)
        self.assertIn("| **all** |", report)

    def test_without_timing(self) -> None:
        result = {"engine": "gpu", "time_ms": {"total": 1}, "best": {"wrong": 0},
                  "history": [{"best_fitness": 1, "eval_ms": 2, "repro_ms": 3}]}
        self.assertEqual(run.without_timing(result), {"best": {"wrong": 0}, "history": [{"best_fitness": 1}]})
        other = json.loads(json.dumps(result))
        other["engine"], other["history"][0]["eval_ms"] = "cpu", 99
        self.assertTrue(run.identical_results(result, other))
        other["best"]["wrong"] = 1
        self.assertFalse(run.identical_results(result, other))


if __name__ == "__main__":
    unittest.main()
