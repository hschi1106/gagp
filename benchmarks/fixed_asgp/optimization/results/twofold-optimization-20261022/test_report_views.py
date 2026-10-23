"""Readable report views retain incomplete coverage and escape task labels."""
import unittest

import report_views


class TestReportViews(unittest.TestCase):
    def test_dashboard_preserves_failures_and_safe_json(self):
        payload=dict(speed_freeze=dict(tasks=['task</script>']),speed=dict(summary=dict(
            cells=[dict(task='task</script>',population=1024,qualified=False,repetitions=0,
                        failures=['budget_not_run'],speedup=None)],
            groups=dict(pooled=dict(observed_geometric_mean=None,qualified_cells=0,expected_cells=1)),
            all_primary_targets_reached=False,native_audit_qualified=0,native_audit_expected=2)),
            quality=dict(runs=[],clustered_bound=dict(success_difference_lower_bound=-1,
                supports_noninferiority=False,unknown_pairs=1)))
        page=report_views.dashboard(payload)
        self.assertIn('budget_not_run',page)
        self.assertIn('task\\u003c/script>',page)
        self.assertNotIn('task</script>',page)
        self.assertIn('id="filter"',page);self.assertIn('id="task"',page);self.assertIn('id="split"',page)
        self.assertIn('overflow-x:auto',page)

    def test_missing_numbers_are_not_zero(self):
        self.assertEqual(report_views.number(None),'—')
        self.assertEqual(report_views.number(0),'0.00')


if __name__=='__main__':unittest.main()
