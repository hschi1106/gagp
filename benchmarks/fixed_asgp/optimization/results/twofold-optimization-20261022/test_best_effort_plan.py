import unittest

import best_effort_plan as plan


class TestBestEffortPlan(unittest.TestCase):
    def refusal(self):
        return dict(choice=None, epoch=10., deadline_epoch=100000., report_reserve_s=10800.,
                    speed_costs_s={'8': 12000., '16': 20000., '32': 40000.},
                    quality_per_seed={'total_s': 700.}, final_regression_and_fixed_floor_s=1800.)

    def test_minimum_full_grid_remains_explicitly_unqualified(self):
        result = plan.minimum_grid(self.refusal(), 80000.)
        self.assertEqual((result['speed_generations'], result['speed_repetitions']), (8, 2))
        self.assertEqual(result['quality_seeds'], list(range(25, 35)))
        self.assertEqual(result['quality_generations'], 64)
        self.assertFalse(result['original_forecast_passed'])
        self.assertTrue(result['planned_full_grid'])
        self.assertGreater(result['estimated_total_s'], result['available_s'])
        self.assertEqual(result['safety_factor'], 1.35)

    def test_fitting_standard_plan_or_expired_reserve_cannot_start_fallback(self):
        data = self.refusal()
        data['choice'] = {'speed_generations': 8}
        with self.assertRaises(ValueError):
            plan.minimum_grid(data, 80000.)
        self.assertIsNone(plan.minimum_grid(self.refusal(), 90000.))
        with self.assertRaises(ValueError):
            plan.minimum_grid(self.refusal(), 0.)

    def test_budget_timeout_is_unknown_but_native_discrepancies_are_rejected(self):
        control = dict(status='stopped', started_epoch=0., finished_epoch=3000., calibration_limit_s=3000.,
                       error="AssertionError('native calibration failed; retain evidence')")
        stages = [{'jobs': {'x': dict(status='timeout')}}]
        self.assertTrue(plan.calibration_allows_validation(control, stages))
        for status, mismatch in [('native_fidelity_mismatch', {}), ('ok', {'fitness': 1}),
                                 ('invalid_output', {}), ('ok', {})]:
            stages = [{'jobs': {'x': dict(status=status, native_mismatches=mismatch)}}]
            with self.assertRaises(ValueError):
                plan.calibration_allows_validation(control, stages)
        with self.assertRaises(ValueError):
            plan.calibration_allows_validation(dict(control, finished_epoch=100.), [])
        with self.assertRaisesRegex(ValueError, 'not the declared budget'):
            plan.calibration_allows_validation(dict(control, error='source changed'),
                                               [{'jobs': {'x': dict(status='timeout')}}])
