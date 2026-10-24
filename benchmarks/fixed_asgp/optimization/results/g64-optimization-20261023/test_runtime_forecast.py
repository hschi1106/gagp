import copy
import unittest

import runtime_forecast
import serial


class TestPreheldoutForecast(unittest.TestCase):
    def fixture(self):
        return {k: 1.0 for k in ("quality_prepare_seed_s", "quality_search_seed_s", "quality_judge_seed_s",
                "speed_search_s", "speed_audit_s", "secondary_prepare_s", "secondary_search_s",
                "secondary_audit_s", "source_checks_s", "fixed_s")}

    def test_selects_largest_fitting_global_cohort(self):
        costs = self.fixture()
        for seeds in (80, 40, 20, 10):
            available = 1.35 * (3 * seeds + 7) + 0.1
            result = runtime_forecast.choose_scale(serial.DEADLINE - 14400 - available, costs)
            self.assertEqual(result["selected_seed_count"], seeds)
            self.assertEqual(result["report_reserve_s"], 14400)
            self.assertEqual(result["deadline_epoch"], serial.DEADLINE)

    def test_full_minimum_grid_refusal_is_not_a_smaller_generation_or_task_grid(self):
        result = runtime_forecast.choose_scale(serial.DEADLINE - 14400 - 1, self.fixture())
        self.assertIsNone(result["selected_seed_count"])
        self.assertEqual([r["seed_count"] for r in result["plans"]], [80, 40, 20, 10])
        self.assertTrue(all(not r["fits"] for r in result["plans"]))

    def test_missing_or_censored_cost_is_not_zero(self):
        for value in (None, 0, -1, float("nan"), float("inf")):
            costs = self.fixture()
            costs["secondary_audit_s"] = value
            with self.assertRaises(ValueError):
                runtime_forecast.choose_scale(serial.START + 3600, costs)
        costs = self.fixture()
        costs.pop("quality_judge_seed_s")
        with self.assertRaises(ValueError):
            runtime_forecast.choose_scale(serial.START + 3600, costs)

    def test_original_clock_policy_and_safety_are_fixed(self):
        for now in (serial.START - 1, serial.DEADLINE, float("nan")):
            with self.assertRaises(ValueError):
                runtime_forecast.choose_scale(now, self.fixture())
        with self.assertRaises(ValueError):
            runtime_forecast.choose_scale(serial.START + 1, self.fixture(), seed_counts=(20, 10))
        with self.assertRaises(ValueError):
            runtime_forecast.choose_scale(serial.START + 1, self.fixture(), safety=1.0)


if __name__ == "__main__":
    unittest.main()
