import unittest

import bottlenecks


class TestBottlenecks(unittest.TestCase):
    def test_scenario_pays_cold_and_final_once(self):
        result=bottlenecks.scenario(dict(search_s=20.),dict(search_s=16.,eval_s=12.))
        self.assertTrue(result['valid'])
        self.assertEqual(result['scenario_search_s'],10.)
        self.assertEqual(result['scenario_speedup_over_baseline'],2.)
        self.assertEqual(result['required_generation_eval_fraction_removed_for2x'],.5)
        self.assertIn('unmeasured',result['scope'])

    def test_overlapping_timers_are_not_a_bound(self):
        self.assertFalse(bottlenecks.scenario(dict(search_s=20.),dict(search_s=16.,eval_s=20.))['valid'])
        with self.assertRaises(ValueError):bottlenecks.scenario({},dict(search_s=16.,eval_s=12.),.5)


if __name__=='__main__':unittest.main()
