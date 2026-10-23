import copy
import math
import unittest

import quality_timing


class TestQualityTiming(unittest.TestCase):
    def fixture(self):
        runs = []
        pairs = []
        for task in ['a', 'b']:
            for seed in [25, 26]:
                ratio = 4. if (task, seed) == ('a', 26) else 1.
                for variant, seconds in [('baseline', ratio), ('candidate', 1.)]:
                    runs.append(dict(task=task, seed=seed, variant=variant, population=1024,
                        generations=64, status='ok', quality_audited=True, search_s=seconds))
                pairs.append(dict(task=task, seed=seed, complete_pair=True, execution_fidelity=True))
        return runs, pairs

    def summarize(self, runs, pairs):
        return quality_timing.summarize(runs, pairs, ['a', 'b'], [25, 26], 1024, 64)

    def test_equal_task_and_paired_seed_geometric_weighting(self):
        result = self.summarize(*self.fixture())
        self.assertAlmostEqual(result['all_task_geometric_mean'], math.sqrt(2.))
        self.assertAlmostEqual(result['tasks'][0]['observed_geometric_mean'], 2.)
        self.assertEqual(result['tasks'][0]['observed_seed_ratio_range'], [1., 4.])
        self.assertTrue(result['full_coverage'])
        self.assertFalse(result['primary_target_proven'])

    def test_missing_search_or_native_judge_cannot_yield_full_task_mean(self):
        for failure in ['missing_search', 'native_incomplete', 'trajectory_different']:
            runs, pairs = self.fixture()
            if failure == 'missing_search':
                runs.pop(0)
            elif failure == 'native_incomplete':
                runs[0]['quality_audited'] = False
                pairs[0]['complete_pair'] = False
            else:
                pairs[0].update(complete_pair=False, execution_fidelity=False)
            original = copy.deepcopy((runs, pairs))
            result = self.summarize(runs, pairs)
            self.assertEqual((runs, pairs), original)
            self.assertEqual(len(result['pairs']), 4)
            self.assertEqual(result['unknown_pairs'], 1)
            self.assertFalse(result['full_coverage'])
            self.assertIsNone(result['all_task_geometric_mean'])

    def test_invalid_times_workload_and_duplicate_keys_are_rejected(self):
        for value in [0., -1., float('nan'), float('inf'), True]:
            runs, pairs = self.fixture()
            runs[0]['search_s'] = value
            with self.assertRaises(ValueError):
                self.summarize(runs, pairs)
        runs, pairs = self.fixture()
        with self.assertRaises(ValueError):
            self.summarize(runs + [runs[0]], pairs)
        with self.assertRaises(ValueError):
            self.summarize(runs, pairs + [pairs[0]])
        runs[0]['population'] = 16384
        with self.assertRaises(ValueError):
            self.summarize(runs, pairs)
