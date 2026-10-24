import unittest

import capacity_after_selection
import runtime_forecast
import serial


class TestCapacityBudget(unittest.TestCase):
    def test_capacity_allowance_cannot_borrow_the_formal_grid_or_reporting_reserve(self):
        costs = {key: 1.0 for key in ("quality_prepare_seed_s", "quality_search_seed_s", "quality_judge_seed_s",
            "speed_search_s", "speed_audit_s", "secondary_prepare_s", "secondary_search_s",
            "secondary_audit_s", "source_checks_s", "fixed_s")}
        now = serial.DEADLINE - 14400 - 150
        self.assertIsNotNone(runtime_forecast.choose_scale(now, costs)["selected_seed_count"])
        value = capacity_after_selection.budget_after_pilot(now, costs)
        self.assertIsNone(value["selected_seed_count"])
        self.assertEqual(value["deadline_epoch"], serial.DEADLINE)
        self.assertEqual(value["report_reserve_s"], 14400)


if __name__ == "__main__":
    unittest.main()
