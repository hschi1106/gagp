import unittest

from report import reference_counts, summarize, validate_configuration, wilson


class TestEvidence(unittest.TestCase):
    def record(self, **changes):
        row = dict(task="fibonacci", seed=0, status="completed", returncode=0,
                   success=True, train_solved=True, raw_success=True, evaluations=1000,
                   seconds=0.01, nodes_before=17, nodes_after=10, train_cases=21, test_cases=31)
        return dict(row, **changes)

    def summary(self, rows):
        return summarize(rows, [0, 1, 2], dict(id="fibonacci", scheme="DP", paper_native=23))

    def test_missing_and_failed_runs_are_not_search_failures(self):
        result = self.summary([self.record(), self.record(seed=1, status="infrastructure_failure")])
        self.assertEqual(result["completed"], 1)
        self.assertEqual(result["successes"], 1)
        self.assertEqual(result["infrastructure_failures"], 1)
        self.assertEqual(result["missing_seeds"], [2])
        self.assertEqual(result["wilson95"], wilson(1, 1))

    def test_overfit_and_initial_success_are_separate(self):
        result = self.summary([self.record(), self.record(seed=1, success=False, evaluations=4996)])
        self.assertEqual(result["initial_population_successes"], 1)
        self.assertEqual(result["overfit"], 1)

    def test_duplicate_and_outside_seeds_rejected(self):
        for rows in ([self.record(), self.record()], [self.record(seed=3)]):
            with self.assertRaises(ValueError):
                self.summary(rows)

    def test_budget_overflow_and_exit_failure_rejected(self):
        for changes in (dict(evaluations=500001), dict(returncode=1), dict(success="true")):
            with self.assertRaises(ValueError):
                self.summary([self.record(**changes)])

    def test_empty_study_has_no_estimated_success_rate(self):
        result = self.summary([])
        self.assertIsNone(result["wilson95"])
        self.assertEqual(result["missing_seeds"], [0, 1, 2])

    def test_data_profile_requires_explicit_matching_expectation(self):
        protocol = dict(evaluations=500000, data_seed=20260626, threads=4)
        config = dict(unified=False, data_profile="count-aligned", simplify=True,
                      evaluations=500000, base_sizes=[0], data_seed=20260626, threads=4)
        with self.assertRaises(ValueError):
            validate_configuration(config, protocol, "legacy")
        validate_configuration(config, protocol, "count-aligned")

    def test_explicit_profile_does_not_allow_other_settings_to_drift(self):
        protocol = dict(evaluations=500000, data_seed=20260626, threads=4)
        config = dict(unified=False, data_profile="count-aligned", simplify=True,
                      evaluations=500000, base_sizes=[0], data_seed=20260626, threads=4)
        for changes in (dict(evaluations=1000), dict(base_sizes=[4]), dict(threads=1)):
            with self.assertRaises(ValueError):
                validate_configuration(dict(config, **changes), protocol, "count-aligned")

    def test_paper_and_thesis_dp_denominators_are_not_interchanged(self):
        row = dict(scheme="DP", paper_successes_out_of_100=23, thesis_successes_out_of_25=23)
        self.assertEqual(reference_counts(row, "short-paper"), (23, 100))
        self.assertEqual(reference_counts(row, "bundled-thesis"), (23, 25))

    def test_thesis_dc_uses_100_run_denominator(self):
        row = dict(scheme="DC", paper_successes_out_of_100=23, thesis_successes_out_of_25=None)
        self.assertEqual(reference_counts(row, "bundled-thesis"), (23, 100))


if __name__ == "__main__":
    unittest.main()
