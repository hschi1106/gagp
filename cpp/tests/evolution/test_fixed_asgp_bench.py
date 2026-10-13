"""Integration checks for the optional external-ASGP benchmark adapter."""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv.pop(1)).resolve()
ROOT = Path(sys.argv.pop(1)).resolve()
sys.path.insert(0, str(ROOT / "tools"))
from gagp_tools.experiments.fixed_asgp.grammar import TASKS, definition


class TestFixedAsgpNative(unittest.TestCase):
    def call(self, *args, env=None):
        result = subprocess.run([str(BINARY), *map(str, args)], capture_output=True, text=True, env=env)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_three_schemes_known_solutions_and_five_modes(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            for name in TASKS:
                with self.subTest(task=name):
                    source = folder / f"{name}.source.json"
                    grammar = folder / f"{name}.grammar.json"
                    prepared = folder / f"{name}.prepared.json"
                    audit = folder / f"{name}.audit.json"
                    grammar.write_text(json.dumps(definition(ROOT, name)))
                    self.call("freeze", name, source)
                    raw = json.loads(source.read_text())
                    self.assertEqual(len(raw["population"]), 8192)
                    self.assertEqual(len(raw["cases"]), 1024)
                    self.call("prepare", source, grammar, 8, prepared)
                    self.assertEqual(len(json.loads(prepared.read_text())["programs"]), 8)
                    if name == "house_robber":
                        # This frozen parent exceeded 1024 physical nodes when
                        # every StateValue included a redundant upper clamp.
                        boundary = dict(raw, population=[raw["population"][2255]])
                        boundary_source = folder / "boundary.source.json"
                        boundary_prepared = folder / "boundary.prepared.json"
                        boundary_source.write_text(json.dumps(boundary))
                        self.call("prepare", boundary_source, grammar, 1, boundary_prepared)
                        self.assertEqual(len(json.loads(boundary_prepared.read_text())["programs"]), 1)
                    self.call("audit", source, grammar, audit)
                    checked = json.loads(audit.read_text())
                    self.assertEqual(checked["known_solution_asgp_wrong"], 0)
                    self.assertEqual(checked["known_solution_cpu_mismatches"], 0)
                    if name != "sum_of_elements":
                        self.assertEqual(checked["known_solution_gpu_fitness"], 0)
                    aligned_initial = []
                    for mode in ("asgp_1t", "gagp_cpu", "gpu_eval", "gpu_repro", "gpu_overlap"):
                        output = folder / f"{name}.{mode}.jsonl"
                        self.call("measure", source, prepared, grammar, 8, mode, output)
                        rows = [json.loads(line) for line in output.read_text().splitlines()]
                        self.assertEqual([r["rep"] for r in rows], [-1, 0, 1, 2])
                        self.assertTrue(all(r["cases"] == 1024 and r["population"] == 8 for r in rows))
                        self.assertTrue(all(r["generation_ms"] > 0 for r in rows))
                        if mode != "asgp_1t":
                            self.assertTrue(all(r["evolve_wall_ms"] >= r["evolve_call_ms"] for r in rows))
                        if name == "median":
                            search_output = folder / f"{name}.{mode}.search.json"
                            env = dict(os.environ, GAGP_SEARCH_GENERATIONS="2", GAGP_BM_SEED="0", GAGP_HOST_THREADS="1")
                            self.call("measure", source, prepared, grammar, 8, "search_" + mode, search_output, env=env)
                            report = json.loads(search_output.read_text())
                            self.assertEqual(report["mode"], mode)
                            self.assertEqual(report["population"], 8)
                            self.assertEqual(report["cases"], 1024)
                            self.assertEqual([r["generation"] for r in report["generations"]], [0, 1])
                            sign = -1 if mode == "asgp_1t" else 1
                            self.assertEqual(report["generations"][0]["best"], sign * rows[0]["best_native_fitness"])
                            self.assertEqual(report["generations"][0]["mean"], sign * rows[0]["mean_native_fitness"])
                            self.assertEqual(len(report["final_population"]), 8)
                            self.assertGreater(report["final_eval_ms"], 0)
                            self.assertGreater(report["evolve_wall_ms"], sum(r["generation_ms"] for r in report["generations"]))
                            self.assertTrue(all(r["expected_program_cases"] == 8192 for r in report["generations"]))
                            self.assertLess(report["generations"][0]["elapsed_search_ms"], report["generations"][1]["elapsed_search_ms"])
                            if mode != "asgp_1t":
                                self.assertEqual(report["cpu_audit_mismatches"], 0)
                            exact_output = folder / f"{name}.{mode}.exact.json"
                            env["GAGP_SEARCH_PENALTY"] = "1"
                            self.call("measure", source, prepared, grammar, 8, "search_" + mode, exact_output, env=env)
                            exact = json.loads(exact_output.read_text())
                            aligned_initial.append((exact["generations"][0]["best"], exact["generations"][0]["mean"]))
                            if mode != "asgp_1t":
                                self.assertEqual(exact["case_penalty"], 1)
                                self.assertEqual(exact["cpu_audit_mismatches"], 0)
                                self.assertGreaterEqual(exact["final_best"], -1024)
                    if name == "median":
                        native_snapshot = folder / "asgp.snapshot.json"
                        self.call("measure", source, prepared, grammar, 8, "asgp_snapshot", native_snapshot)
                        native_scores = json.loads(native_snapshot.read_text())["programs"]
                        self.assertEqual(max(r["fitness"] for r in native_scores), aligned_initial[0][0])
                        self.assertTrue(all(r["fitness"] == -r["wrong"] and r["cases"] == 1024 for r in native_scores))
                        self.assertTrue(all(pair == aligned_initial[0] for pair in aligned_initial))
                        snapshot = folder / "exact.snapshot.json"
                        env = dict(os.environ, GAGP_SNAPSHOT_PENALTY="1", GAGP_SNAPSHOT_CPU="1")
                        self.call("measure", source, prepared, grammar, 8, "snapshot", snapshot, env=env)
                        scores = json.loads(snapshot.read_text())["programs"]
                        self.assertEqual(max(r["fitness"] for r in scores), aligned_initial[0][0])
                        self.assertTrue(all(r["fitness"] == r["cpu_fitness"] for r in scores))
                        for value in ("0", "-1", "nan", "inf"):
                            env["GAGP_SNAPSHOT_PENALTY"] = value
                            rejected = subprocess.run([str(BINARY), "measure", str(source), str(prepared), str(grammar), "8", "snapshot", str(snapshot)], env=env, capture_output=True, text=True)
                            self.assertNotEqual(rejected.returncode, 0)





if __name__ == "__main__":
    unittest.main()
