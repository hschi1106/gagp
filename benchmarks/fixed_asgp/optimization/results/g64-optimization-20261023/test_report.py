import csv
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import report
import report_metrics
import serial


class TestHonestReport(unittest.TestCase):
    def test_speed_cohort_quality_keeps_failed_and_unadmitted_tasks_unknown(self):
        jobs = [dict(task="a", variant=v, repetition=0, status="ok" if v == "baseline" else "evidence_invalid",
                     search_status="ok") for v in ("baseline", "candidate", "asgp")]
        known = [dict(task="a", variant="baseline", initial_success=False, final_success=True, newly_solved=True)]
        rows = report.closed_speed_quality(known, jobs, ["a"])
        self.assertEqual(len(rows), 3)
        self.assertTrue(rows[0]["fully_audited"])
        self.assertEqual(rows[2]["status"], "evidence_invalid")
        self.assertIsNone(rows[2]["final_success"])

    def test_report_describes_only_the_selected_execution_methods(self):
        methods = report.execution_methods(dict(GAGP_GPU_DENSE_CASE_TILES="tuple", GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT="0"))
        self.assertEqual(len(methods), 1)
        self.assertIn("TupleSummary", methods[0])
        self.assertNotIn("編譯期", methods[0])
        methods = report.execution_methods(dict(GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT="1", GAGP_GPU_CASE_VIEW_BLOCK="hybrid"))
        self.assertEqual(len(methods), 2)
        self.assertTrue(any("編譯期" in text for text in methods))

    def test_interrupted_preparation_retains_full_task_grid(self):
        with tempfile.TemporaryDirectory() as folder, mock.patch.object(serial, "RAW", Path(folder)):
            result = report.prepared_inputs("interrupted", dict(tasks=["a", "b"], _sha256="frozen"))
        self.assertEqual(set(result), {"a", "b"})
        self.assertTrue(all(row["status"] == "missing" for row in result.values()))

    def test_unknown_pairs_are_not_agreement_for_uncertainty(self):
        seeds = list(range(35, 115))
        pairs = [dict(task="a", seed=s, complete_pair=False, baseline_success=False, candidate_success=False) for s in seeds]
        tasks, bound = report.quality_summary([], pairs, ["a"], seeds)
        self.assertEqual(tasks[0]["quality_complete"], 0)
        self.assertLess(tasks[0]["success_difference_ci95"][1], 0)
        self.assertFalse(bound["supports_noninferiority"])
        self.assertEqual(bound["unknown_pairs"], 80)

    def test_curves_use_same_completed_seed_pairs(self):
        rows = [dict(task="a", seed=35, variant="baseline", complete=True, initial_success=False,
                     final_success=True, newly_solved=True, train_accuracy_history=[1]*65, test_accuracy_history=[1]*65)]
        pair = dict(task="a", seed=35, complete_pair=False, baseline_success=True, candidate_success=False)
        tasks, _ = report.quality_summary(rows, [pair], ["a"], [35])
        self.assertEqual(tasks[0]["curves"], {"baseline": {}, "candidate": {}})
        self.assertEqual(tasks[0]["counts"]["baseline"]["final"], 1)

    def test_csv_retains_failure_and_nested_evidence_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "results.csv"
            report.csv_file(path, [dict(task="a", status="native_unadmitted", mismatches={"wrong": 2}),
                                   dict(task="b", status="budget_not_run")])
            with path.open(newline="") as stream:
                rows = list(csv.DictReader(stream))
            self.assertEqual([row["status"] for row in rows], ["native_unadmitted", "budget_not_run"])
            self.assertIn('"wrong": 2', rows[0]["mismatches"])
            with self.assertRaises(FileExistsError):
                report.csv_file(path, [])

    def test_secondary_asgp_budget_not_run_prevents_a_complete_study_claim(self):
        # Every GAGP pair and all primary/quality work can pass while one P16384
        # ASGP job is unrun. Its absence must not disappear behind GAGP coverage.
        cells = [dict(task=t, population=16384, qualified=True, speedup=1.2) for t in ("a", "b")]
        pairs = [dict(task=t, population=16384, repetition=0, statuses=["ok", "ok"],
                      asgp_over_candidate=2, asgp_status="ok") for t in ("a", "b")]
        complete_group = dict(full_coverage=True)
        _, secondary = report_metrics.secondary_display(pairs, cells, ["a", "b"])
        self.assertTrue(report.experiment_complete(complete_group, True, True, complete_group, secondary, []))
        pairs[-1].update(asgp_over_candidate=None, asgp_status="budget_not_run")
        _, secondary = report_metrics.secondary_display(pairs, cells, ["a", "b"])
        self.assertFalse(report.experiment_complete(complete_group, True, True, complete_group, secondary, []))


if __name__ == "__main__":
    unittest.main()
