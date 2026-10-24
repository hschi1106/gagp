import copy
import unittest

import freeze_and_run


class TestConfirmedCandidate(unittest.TestCase):
    def fixture(self):
        development = dict(status="requested_phase_complete", task_count=30, performance_admitted=True,
                           binary="selected-immutable", binary_sha256="a" * 64,
                           candidate_extra={"GAGP_GPU_DENSE_CASE_LANES": "1"})
        confirmation = dict(status="requested_phase_complete", comparisons=90, geometric_mean=1.31,
                            binary_sha256="a" * 64, candidate_extra=copy.deepcopy(development["candidate_extra"]))
        return development, confirmation

    def test_only_confirmed_binary_and_whole_global_settings_are_returned(self):
        development, confirmation = self.fixture()
        result = freeze_and_run.confirmed_candidate(development, confirmation)
        self.assertEqual(result, dict(binary="selected-immutable", binary_sha256="a" * 64,
                                      extra={"GAGP_GPU_DENSE_CASE_LANES": "1"}))

    def test_source_binary_scope_and_parity_completion_cannot_be_substituted(self):
        development, confirmation = self.fixture()
        for key, value in (("status", "stopped"), ("task_count", 8), ("performance_admitted", False)):
            bad = copy.deepcopy(development)
            bad[key] = value
            with self.assertRaises(AssertionError):
                freeze_and_run.confirmed_candidate(bad, confirmation)
        for key, value in (("binary_sha256", "b" * 64), ("candidate_extra", {}), ("comparisons", 30),
                           ("status", "running"), ("geometric_mean", float("nan")), ("geometric_mean", .99)):
            bad = copy.deepcopy(confirmation)
            bad[key] = value
            with self.assertRaises(AssertionError):
                freeze_and_run.confirmed_candidate(development, bad)


if __name__ == "__main__":
    unittest.main()
