from copy import deepcopy
import ast
from pathlib import Path
from unittest import TestCase

import dashboard_data


class TestCompactDashboard(TestCase):
    def bundle(self):
        return dict(speed_freeze={'tasks': ['task_a', 'task_b']},
            speed={'summary': dict(cells=[], all_primary_targets_reached=False,
                native_audit_qualified=0, native_audit_expected=1,
                groups={'pooled': dict(observed_geometric_mean=1.44, qualified_cells=0, expected_cells=60)})},
            quality={'runs': [
                dict(task='task_a', variant='baseline', quality_audited=True,
                     joint_gen0=True, joint_final=False, joint_lost_initial=True,
                     train_gen0=True, train_final=True, test_gen0=True, test_final=False),
                dict(task='task_a', variant='baseline', quality_audited=False,
                     joint_final=True, train_final=True, test_final=True),
                dict(task='task_a', variant='candidate', quality_audited=True,
                     joint_gen0=False, joint_final=True, joint_new_final=True,
                     train_gen0=False, train_final=True, train_new_final=True,
                     test_gen0=False, test_final=True, test_new_final=True)],
                'clustered_bound': {'unknown_pairs': 1}})

    def test_failed_and_unknown_runs_stay_expected_but_not_audited_or_successful(self):
        payload = dashboard_data.compact_payload(self.bundle())
        old = payload['quality_counts'][0]
        self.assertEqual((old['expected'], old['audited']), (2, 1))
        self.assertEqual(old['joint'], dict(gen0=1, final=0, new_final=0, lost_initial=1))
        self.assertEqual(old['train']['final'], 1)
        self.assertEqual(old['test']['final'], 0)

    def test_split_and_new_success_counts_are_retained(self):
        new = dashboard_data.compact_payload(self.bundle())['quality_counts'][1]
        for split in ('joint', 'train', 'test'):
            self.assertEqual(new[split], dict(gen0=0, final=1, new_final=1, lost_initial=0))

    def test_missing_tasks_and_methods_remain_zero_not_dropped(self):
        payload = dashboard_data.compact_payload(self.bundle())
        self.assertEqual(len(payload['quality_counts']), 4)
        self.assertEqual(payload['quality_counts'][2]['expected'], 0)

    def test_original_evidence_and_statistical_bound_are_unchanged(self):
        bundle = self.bundle()
        before = deepcopy(bundle)
        result = dashboard_data.compact_payload(bundle)
        self.assertEqual(bundle, before)
        self.assertEqual(result['bound'], bundle['quality']['clustered_bound'])
        self.assertEqual(result['speed']['groups']['pooled']['observed_geometric_mean'], 1.44)
        self.assertNotIn('runs', result)

    def test_unexpected_method_is_rejected(self):
        bundle = self.bundle()
        bundle['quality']['runs'][0]['variant'] = 'unmeasured'
        with self.assertRaises(ValueError):
            dashboard_data.compact_payload(bundle)

    def test_nondashboard_numerical_views_remain_identical(self):
        root = Path(__file__).resolve().parent
        def functions(filename):
            text = (root / filename).read_text()
            return {node.name: ast.get_source_segment(text, node) for node in ast.parse(text).body
                    if isinstance(node, ast.FunctionDef) and node.name != 'dashboard'}
        self.assertEqual(functions('report_views_compact.py'), functions('report_views_implementation.py'))
