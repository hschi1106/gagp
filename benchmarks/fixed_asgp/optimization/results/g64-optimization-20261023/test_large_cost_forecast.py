import copy
import unittest

import runtime_forecast_v2


class TestLargeCostEvidence(unittest.TestCase):
    def fixture(self):
        costs = {kind: dict(status="ok", process_s=1.0, native_fidelity_pass=True)
                 for kind in ("gagp_common", "gagp_native", "asgp_common", "asgp_native")}
        return dict(status="requested_phase_complete", full_cost_observed=True, diagnostic_only=True, new_heldout=False,
                    observations=[dict(task="a", status="measured", original_train_count=200,
                                       probe_count=1000, forecast_test_count=1000, costs=costs)])

    def test_complete_training_probe_is_required(self):
        value = self.fixture()
        self.assertEqual(set(runtime_forecast_v2.large_probe_rows(value, ["a"])), {"a"})
        for key, bad in (("full_cost_observed", False), ("new_heldout", True), ("status", "running")):
            row = copy.deepcopy(value)
            row[key] = bad
            with self.assertRaises(AssertionError):
                runtime_forecast_v2.large_probe_rows(row, ["a"])
        for kind in value["observations"][0]["costs"]:
            row = copy.deepcopy(value)
            row["observations"][0]["costs"][kind]["status"] = "budget_not_run"
            with self.assertRaises(AssertionError):
                runtime_forecast_v2.large_probe_rows(row, ["a"])

    def test_missing_duplicate_or_undersized_probe_cannot_define_full_cost(self):
        value = self.fixture()
        for rows in ([], value["observations"] * 2):
            with self.assertRaises(AssertionError):
                runtime_forecast_v2.large_probe_rows(dict(value, observations=rows), ["a"])
        value["observations"][0]["probe_count"] = 999
        with self.assertRaises(AssertionError):
            runtime_forecast_v2.large_probe_rows(value, ["a"])


if __name__ == "__main__":
    unittest.main()
