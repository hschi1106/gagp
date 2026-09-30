from __future__ import annotations

import copy
import json
import sys
import tempfile
from types import SimpleNamespace
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from gagp_tools.experiments.fixed_asgp.grammar import definition
from gagp_tools.experiments.fixed_asgp.runner import (
    CONTRACT, ROOT, adapter_hash, asgp_source_hash, run, summarize, validate_rows,
)


def samples(mode, times=(999, 10, 20, 30)):
    return [{"task": "median", "population": 1024, "cases": 1024, "mode": mode,
             "rep": rep, "generation_ms": elapsed, "eval_ms": elapsed / 2}
            for rep, elapsed in zip((-1, 0, 1, 2), times)]


class TestFixedAsgp(unittest.TestCase):
    def test_changed_asgp_baseline_rejected_before_measurement(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "asgp"
            (source / "src").mkdir(parents=True)
            code = source / "src" / "baseline.cc"
            code.write_text("original")
            artifacts = root / "artifacts"
            artifacts.mkdir()
            (artifacts / "manifest.json").write_text(json.dumps({
                "contract": CONTRACT, "adapter_sha256": adapter_hash(),
                "asgp_source_sha256": asgp_source_hash(source),
            }))
            code.write_text("changed")
            with self.assertRaisesRegex(ValueError, "ASGP baseline source changed"):
                run(SimpleNamespace(artifacts=artifacts, asgp_source=source))

    def test_warmup_excluded_and_ratios_use_medians(self):
        cells = [{"task": "median", "population": 1024, "mode": mode, "status": "ok",
                  "rows": samples(mode, times)}
                 for mode, times in (("asgp_1t", (1, 20, 40, 60)),
                                     ("gagp_cpu", (1, 10, 20, 30)),
                                     ("gpu_repro", (10000, 2, 4, 6)),
                                     ("gpu_overlap", (10000, 1, 2, 3)))]
        row = summarize(cells)[-1]
        self.assertEqual(row["median_ms"], 2)
        self.assertEqual(row["speedup_asgp_1t"], 20)
        self.assertEqual(row["speedup_gagp_cpu"], 10)
        self.assertEqual(row["overlap_speedup"], 2)

    def test_failed_baseline_never_produces_ratio(self):
        rows = summarize([
            {"task": "median", "population": 1024, "mode": "asgp_1t", "status": "failed", "rows": []},
            {"task": "median", "population": 1024, "mode": "gpu_eval", "status": "ok", "rows": samples("gpu_eval")},
        ])
        self.assertNotIn("speedup_asgp_1t", rows[1])

    def test_incomplete_duplicate_or_wrong_workload_rejected(self):
        good = samples("gpu_eval")
        for bad in (good[:-1], good + [good[-1]], [good[0], good[1], good[1], good[3]]):
            with self.assertRaises(ValueError):
                validate_rows(bad, "median", 1024, "gpu_eval")
        for field, value in (("cases", 128), ("population", 2048), ("mode", "gpu_repro"),
                             ("task", "house_robber"), ("generation_ms", float("nan"))):
            bad = copy.deepcopy(good)
            bad[1][field] = value
            with self.assertRaises(ValueError):
                validate_rows(bad, "median", 1024, "gpu_eval")

    def test_task_skeletons_have_distinct_required_behavior(self):
        median = definition(ROOT, "median")["templates"][0]["body"]
        self.assertEqual(median["args"][2]["signature"], "le(Int,Int)->Bool")
        self.assertEqual(median["args"][2]["args"][1]["constant"]["values"], ["3"])
        dc = definition(ROOT, "sum_of_elements")["templates"][0]["body"]["structured"]["plan"]
        self.assertFalse(dc["memoized"])
        dp = definition(ROOT, "house_robber")["templates"][0]["body"]
        plan = dp["structured"]["plan"]
        self.assertTrue(plan["memoized"])
        self.assertEqual([r["states"][0]["offset"] for r in plan["requests"]], ["-1", "-2"])
        self.assertEqual(plan["parameter_types"], ["IntList"])
        self.assertEqual(dp["captures"], [{"input": "source"}])


if __name__ == "__main__":
    unittest.main()
