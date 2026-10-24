import copy
import unittest

import report_metrics


class TestClosedReportMetrics(unittest.TestCase):
    def test_missing_and_failed_quality_searches_remain_in_the_frozen_denominator(self):
        jobs = [dict(task="a", seed=35, variant="baseline", status="interrupted")]
        rows = report_metrics.closed_quality_grid(jobs, ["a", "b"], [35, 36])
        self.assertEqual(len(rows), 8)
        self.assertEqual(sum(r["status"] == "missing" for r in rows), 7)
        self.assertEqual(rows[0]["status"], "interrupted")
        for bad in (jobs + jobs, jobs + [dict(task="foreign", seed=35, variant="candidate")],
                    [dict(jobs[0], generations=8)]):
            with self.assertRaises(ValueError):
                report_metrics.closed_quality_grid(bad, ["a", "b"], [35, 36])

    def fixture(self, initial=False, final=True):
        common_rows = [dict(cases=2, correct_count=2 if ok else 1, wrong=0 if ok else 1,
                            runtime_errors=0, exact_accuracy=1 if ok else .5)
                       for ok in [initial] + [False] * 63 + [final]]
        native = [dict(program=i, cases=2, cpu_cases=2, cpu_wrong=row["wrong"], cpu_errors=0, cpu_timeouts=0)
                  for i, row in enumerate(common_rows)]
        common = dict(history=common_rows[:64], final=common_rows[-1])
        return common, copy.deepcopy(common), dict(programs=native), dict(programs=copy.deepcopy(native))

    def test_initial_success_is_separate_from_new_evolution_success(self):
        result = report_metrics.champion_outcomes(*self.fixture())
        self.assertFalse(result["initial_success"])
        self.assertTrue(result["newly_solved"])
        self.assertEqual(result["first_joint_success_generation"], 64)
        result = report_metrics.champion_outcomes(*self.fixture(True, True))
        self.assertTrue(result["initial_success"])
        self.assertFalse(result["newly_solved"])
        self.assertEqual(result["first_joint_success_generation"], 0)

    def test_native_wrong_or_test_runtime_failure_prevents_a_solved_claim(self):
        docs = self.fixture(True, True)
        docs[-1]["programs"][-1]["cpu_wrong"] = 1
        result = report_metrics.champion_outcomes(*docs)
        self.assertFalse(result["final_success"])
        self.assertTrue(result["lost_initial"])
        docs = self.fixture()
        docs[1]["final"]["runtime_errors"] = 1
        self.assertFalse(report_metrics.champion_outcomes(*docs)["final_success"])

    def test_missing_champion_or_wrong_case_alignment_is_rejected(self):
        docs = self.fixture()
        docs[2]["programs"].pop()
        with self.assertRaises(ValueError):
            report_metrics.champion_outcomes(*docs)
        docs = self.fixture()
        docs[3]["programs"][0]["cpu_cases"] = 1
        with self.assertRaises(ValueError):
            report_metrics.champion_outcomes(*docs)

    def test_timing_uncertainty_preserves_whole_round_pairing(self):
        result = report_metrics.paired_round_interval([[2, 2, 2], [2, 2, 2]])
        self.assertEqual(result["timing_ci95"], [2, 2])
        self.assertEqual(result["bootstrap_draws"], 27)
        result = report_metrics.paired_round_interval([[1, 2, 4]])
        self.assertAlmostEqual(result["geometric_mean"], 2)
        self.assertLess(result["timing_ci95"][0], 2)
        self.assertGreater(result["timing_ci95"][1], 2)
        with self.assertRaises(ValueError):
            report_metrics.paired_round_interval([[2, 2]])

    def secondary_fixture(self):
        pairs = [dict(task=task, population=16384, repetition=0, asgp_over_candidate=ratio,
                      statuses=["ok", "ok"], asgp_status="ok") for task, ratio in [("a", 2), ("b", 8)]]
        cells = [dict(task=task, population=16384, qualified=True, speedup=1.2) for task in ("a", "b")]
        return pairs, cells, ["a", "b"]

    def test_secondary_asgp_display_uses_all_frozen_tasks(self):
        rows, summary = report_metrics.secondary_display(*self.secondary_fixture())
        self.assertEqual(len(rows), 2)
        self.assertTrue(all(r["complete"] for r in rows))
        self.assertAlmostEqual(summary["asgp_speedup"], 4)
        self.assertEqual(summary["asgp_qualified_tasks"], summary["expected_tasks"])

    def test_secondary_missing_asgp_does_not_become_a_subset_geometric_mean(self):
        pairs, cells, tasks = self.secondary_fixture()
        pairs[1].update(asgp_over_candidate=None, asgp_status="budget_not_run")
        rows, summary = report_metrics.secondary_display(pairs, cells, tasks)
        self.assertEqual(len(rows), 2)
        self.assertIsNone(summary["asgp_speedup"])
        self.assertEqual(summary["asgp_qualified_tasks"], 1)
        self.assertEqual(summary["expected_tasks"], 2)
        self.assertEqual(rows[1]["asgp_status"], "budget_not_run")
        self.assertFalse(rows[1]["complete"])

    def test_secondary_failed_fidelity_hides_timing_only_ratios_and_keeps_failure(self):
        pairs, cells, tasks = self.secondary_fixture()
        pairs[1]["statuses"] = ["ok", "evidence_invalid"]
        cells[1].update(qualified=False, speedup=None)
        rows, summary = report_metrics.secondary_display(pairs, cells, tasks)
        self.assertIsNone(rows[1]["speedup"])
        self.assertIsNone(rows[1]["asgp_over_candidate"])
        self.assertEqual(rows[1]["statuses"], ["ok", "evidence_invalid"])
        self.assertFalse(rows[1]["complete"])
        self.assertIsNone(summary["asgp_speedup"])

    def test_secondary_duplicate_missing_or_wrong_population_cannot_change_scope(self):
        pairs, cells, tasks = self.secondary_fixture()
        for bad in (pairs[:1], [pairs[0], pairs[0]], [dict(pairs[0], population=1024), pairs[1]],
                    [dict(pairs[0], repetition=1), pairs[1]]):
            with self.assertRaises(ValueError):
                report_metrics.secondary_display(bad, cells, tasks)

    def test_unsampled_zero_peak_and_missing_calls_remain_unknown(self):
        rows = [dict(population=16384, variant="baseline", search_status="ok", qualified=True,
                     memory_samples=0, sampled_card_peak_bytes=0, memory_sampling_errors=2),
                dict(population=16384, variant="candidate", search_status="budget_not_run", qualified=False)]
        result = report_metrics.card_memory_summary(rows, 16384)
        self.assertIsNone(result["peak_bytes"])
        self.assertEqual(result["sampled_gpu_searches"], 0)
        self.assertEqual(result["unknown_gpu_searches"], 2)
        self.assertEqual(result["successful_gpu_searches"], 1)
        self.assertEqual(result["qualified_gpu_searches"], 1)
        self.assertEqual(result["recorded_sampling_errors"], 2)
        self.assertEqual(result["unknown_sampling_error_searches"], 1)

    def test_failed_search_peak_is_retained_separately_from_cpu_qualification(self):
        rows = [dict(population=16384, variant="baseline", search_status="ok", qualified=True,
                     memory_samples=3, sampled_card_peak_bytes=10, memory_sampling_errors=0),
                dict(population=16384, variant="candidate", search_status="timeout", qualified=False,
                     memory_samples=2, sampled_card_peak_bytes=20, memory_sampling_errors=1),
                dict(population=16384, variant="asgp", search_status="ok", qualified=True)]
        result = report_metrics.card_memory_summary(rows, 16384)
        self.assertEqual(result["expected_gpu_searches"], 2)
        self.assertEqual(result["sampled_gpu_searches"], 2)
        self.assertEqual(result["peak_bytes"], 20)
        self.assertEqual(result["sampled_failed_searches"], 1)
        self.assertEqual(result["successful_gpu_searches"], 1)
        self.assertEqual(result["qualified_gpu_searches"], 1)

    def test_invalid_sample_fields_cannot_become_capacity_evidence(self):
        row = dict(population=16384, variant="candidate", search_status="ok", qualified=False,
                   memory_samples=1, sampled_card_peak_bytes=20, memory_sampling_errors=0)
        for fields in (dict(memory_samples=True), dict(memory_samples=-1),
                       dict(sampled_card_peak_bytes=-1), dict(sampled_card_peak_bytes=float("nan"))):
            result = report_metrics.card_memory_summary([dict(row, **fields)], 16384)
            self.assertIsNone(result["peak_bytes"])
        with self.assertRaises(ValueError):
            report_metrics.card_memory_summary([dict(row, population=1024)], 16384)


if __name__ == "__main__":
    unittest.main()
