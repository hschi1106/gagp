import unittest

import forecast_measured_native as forecast


class TestMeasuredNativeForecast(unittest.TestCase):
    def rows(self):
        return [dict(task='task', population=p, variant=v, status='ok', elapsed_s=3.,
                     process_s=2., actual_cpu_programs=10 if v == 'baseline' else 0,
                     cpu_reference_verified=v == 'candidate')
                for p in (1024, 16384) for v in ('baseline', 'candidate')]

    def test_complete_cost_counts_both_children_and_parent_work(self):
        self.assertEqual(forecast.complete_native_cost(self.rows(), ['task']), 12.)

    def test_missing_failed_duplicate_and_unproved_reference_are_rejected(self):
        for edit in (lambda rows: rows.pop(), lambda rows: rows.append(rows[0]),
                     lambda rows: rows[0].update(status='failed'),
                     lambda rows: rows[1].update(cpu_reference_verified=False),
                     lambda rows: rows[0].update(elapsed_s=1.)):
            rows = self.rows()
            edit(rows)
            with self.assertRaises(ValueError):
                forecast.complete_native_cost(rows, ['task'])
