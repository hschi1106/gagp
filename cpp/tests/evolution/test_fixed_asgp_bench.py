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
                    for mode in ("asgp_1t", "gagp_cpu", "gpu_eval", "gpu_repro", "gpu_overlap"):
                        output = folder / f"{name}.{mode}.jsonl"
                        self.call("measure", source, prepared, grammar, 8, mode, output)
                        rows = [json.loads(line) for line in output.read_text().splitlines()]
                        self.assertEqual([r["rep"] for r in rows], [-1, 0, 1, 2])
                        self.assertTrue(all(r["cases"] == 1024 and r["population"] == 8 for r in rows))
                        self.assertTrue(all(r["generation_ms"] > 0 for r in rows))
                    if name == "sum_of_elements":
                        output = folder / "fresh-phase-bank.json"
                        env = dict(os.environ, GAGP_BANK_FRESH_MUTATION="1",
                                   GAGP_BANK_GENERATIONS="2", GAGP_GPU_DIAGNOSTICS="1",
                                   GAGP_VIEW_PROFILE="1", GAGP_TYPED_VIEW_PHASE="1")
                        self.call("measure", source, prepared, grammar, 8, "phase_bank", output, env=env)
                        probe = json.loads(output.read_text())
                        self.assertEqual(probe["profile"], "independent-phase-bank-with-fresh-mutation")
                        self.assertEqual(probe["export_fitness_equal"], 1)
                        self.assertEqual(len(probe["final_population"]), 8)
                        self.assertTrue(all(r["cases"] == 8 * 1024 and r["unscored"] == 0
                                            for r in probe["generations"]))
                        self.assertGreaterEqual(probe["generations"][0]["first_generation_with_bank_ms"],
                                                probe["bank_init_ms"])



if __name__ == "__main__":
    unittest.main()
