import unittest

import report_validation3 as validation


class TestPlanningMode(unittest.TestCase):
    def test_best_effort_deviation_is_visible_even_if_all_cells_finish(self):
        freeze = dict(planning_mode='best_effort_after_budget_refusal', original_forecast_passed=False)
        issues = validation.planning_issues(freeze, dict(freeze))
        self.assertEqual(issues[0]['status'], 'best_effort_outside_fitting_rule')
        self.assertEqual(validation.planning_issues({}, {}), [])
        with self.assertRaises(AssertionError):
            validation.planning_issues(freeze, {})
        with self.assertRaises(AssertionError):
            validation.planning_issues(dict(freeze, original_forecast_passed=True), freeze)
