import copy
import unittest

import process_timing


class TestProcessTiming(unittest.TestCase):
    def fixture(self):
        runs = [dict(task='x', population=p, repetition=r, variant=v, status='ok',
                     process_s=(4. if v == 'baseline' else 2.) * (r + 1))
                for p in (1024, 16384) for r in (0, 1) for v in ('baseline', 'candidate')]
        cells = [dict(task='x', population=p, qualified=True) for p in (1024, 16384)]
        return runs, cells

    def test_matched_geometric_process_ratio_does_not_modify_primary_data(self):
        runs, cells = self.fixture()
        original = copy.deepcopy((runs, cells))
        result = process_timing.summarize(runs, cells, ['x'], [1024, 16384], 2)
        self.assertAlmostEqual(result['groups']['pooled']['observed_geometric_mean'], 2.)
        self.assertEqual((runs, cells), original)
        self.assertIn('不替代2×搜尋目標', process_timing.markdown(result))

    def test_unqualified_cells_remain_unqualified(self):
        runs, cells = self.fixture()
        cells[0]['qualified'] = False
        result = process_timing.summarize(runs, cells, ['x'], [1024, 16384], 2)
        self.assertEqual(result['groups']['pooled']['qualified_cells'], 1)
        self.assertEqual(result['groups']['pooled']['expected_cells'], 2)
        self.assertNotIn('speedup', result['cells'][0])

    def test_missing_process_time_or_duplicate_run_cannot_pass(self):
        for edit in (lambda runs: runs.pop(), lambda runs: runs.append(runs[0]),
                     lambda runs: runs[0].update(process_s=0.)):
            runs, cells = self.fixture()
            edit(runs)
            with self.assertRaises(ValueError):
                process_timing.summarize(runs, cells, ['x'], [1024, 16384], 2)
