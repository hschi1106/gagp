import copy
import math
from pathlib import Path
import tempfile
import unittest

import development_evidence
import serial


class TestDevelopmentEvidence(unittest.TestCase):
    def rows(self, family='a', ratio=1.3):
        return [dict(family=family, variant='candidate', task=t, population=p,
                     status='ok', speedup=ratio, final_descriptors_equal=True,
                     full_final_ast_equal=True, champion_ast_fitness_equal=True)
                for t in ['a', 'b'] for p in (1024, 16384)]

    def test_complete_mean_remains_development_not_formal_or_quality(self):
        result = development_evidence.summarize(self.rows(), ['a', 'b'])
        self.assertTrue(result['best_complete']['complete'])
        self.assertAlmostEqual(result['best_complete']['geometric_means']['pooled'], 1.3)
        for field in ('formal_target_proven', 'independent_quality_proven',
                      'full_population_cpu_audit_proven'):
            self.assertIs(result[field], False)

    def test_partial_fast_subset_cannot_replace_complete_recipe(self):
        rows = self.rows() + self.rows('fast', 20)[:-1]
        result = development_evidence.summarize(rows, ['a', 'b'])
        self.assertEqual(result['best_complete']['family'], 'a')
        fast = next(r for r in result['recipes'] if r['family'] == 'fast')
        self.assertIsNone(fast['geometric_means']['pooled'])
        self.assertEqual(fast['qualified_cells'], 3)
        self.assertEqual(fast['expected_cells'], 4)

    def test_missing_or_failed_fidelity_never_qualifies_even_if_over_twofold(self):
        for field in development_evidence.FIDELITY:
            rows = self.rows(ratio=4)
            rows[0][field] = False
            result = development_evidence.summarize(rows, ['a', 'b'])
            self.assertIsNone(result['best_complete'])
            self.assertIsNone(result['recipes'][0]['geometric_means']['pooled'])

    def test_missing_failed_or_invalid_ratio_stays_unqualified(self):
        for value in (None, 0, -2, True, math.inf, math.nan):
            rows = self.rows()
            rows[0]['speedup'] = value
            self.assertIsNone(development_evidence.summarize(rows, ['a', 'b'])['best_complete'])
        for status in ('missing', 'failed', 'budget_not_run', 'running', 'invalid_output'):
            rows = self.rows()
            rows[0]['status'] = status
            result = development_evidence.summarize(rows, ['a', 'b'])
            self.assertIsNone(result['best_complete'])
            self.assertEqual(result['recipes'][0]['qualified_cells'], 3)

    def test_duplicate_unexpected_or_empty_tasks_rejected(self):
        rows = self.rows()
        with self.assertRaises(ValueError):
            development_evidence.summarize(rows + [copy.deepcopy(rows[0])], ['a', 'b'])
        rows[0]['population'] = 4096
        with self.assertRaises(ValueError):
            development_evidence.summarize(rows, ['a', 'b'])
        for tasks in ([], ['a', 'a']):
            with self.assertRaises(ValueError):
                development_evidence.summarize([], tasks)

    def test_empty_inventory_does_not_invent_coverage(self):
        result = development_evidence.summarize([], ['a', 'b'])
        self.assertIsNone(result['best_complete'])
        self.assertEqual(result['recipes'], [])

    def test_population_and_pooled_geomeans_are_distinct(self):
        rows = self.rows()
        for row in rows:
            row['speedup'] = 2 if row['population'] == 1024 else 8
        means = development_evidence.summarize(rows, ['a', 'b'])['best_complete']['geometric_means']
        for key, expected in {'1024': 2.0, '16384': 8.0, 'pooled': 4.0}.items():
            self.assertAlmostEqual(means[key], expected)

    def stage_fixture(self, raw, name, binary, jobs):
        directory = raw / name
        directory.mkdir()
        (directory / 'bench').write_bytes(binary)
        serial.save(directory / 'manifest.json', dict(status='stopped', settings={},
            binary_sha256=serial.sha(directory / 'bench'), jobs=jobs))
        return serial.sha(directory / 'bench')

    def test_failed_arm_without_any_comparisons_is_retained(self):
        with tempfile.TemporaryDirectory() as directory:
            raw = Path(directory)
            digest = self.stage_fixture(raw, 'f-p1024-baseline', b'baseline', {})
            self.stage_fixture(raw, 'f-p1024-candidate', b'candidate',
                               {'a.p1024.s0.g8': dict(status='failed')})
            result = development_evidence.read_families(['f'],
                dict(tasks=['a', 'b'], settings={}, baseline_sha256=digest), raw)
            self.assertEqual(len(result['cells']), 4)
            self.assertEqual(result['cells'][0]['status'], 'failed_or_unfinished')
            self.assertEqual(result['recipes'][0]['qualified_cells'], 0)
            self.assertIsNone(result['best_complete'])

    def test_other_denominator_rejected_even_when_arm_failed(self):
        with tempfile.TemporaryDirectory() as directory:
            raw = Path(directory)
            self.stage_fixture(raw, 'f-p1024-baseline', b'other-binary', {})
            self.stage_fixture(raw, 'f-p1024-candidate', b'candidate', {})
            with self.assertRaisesRegex(AssertionError, 'denominator'):
                development_evidence.read_families(['f'],
                    dict(tasks=['a'], settings={}, baseline_sha256='not-the-baseline'), raw)


if __name__ == '__main__':
    unittest.main()
