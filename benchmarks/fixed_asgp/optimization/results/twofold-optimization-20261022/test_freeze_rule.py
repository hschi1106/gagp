import unittest

from freeze_rule import choose_scale


class TestTrainingOnlyScaleChoice(unittest.TestCase):
    def test_quality_seed_power_has_priority_over_speed_generations(self):
        #80seeds+G8 fits;40seeds+G32 also fits. Preserve80 independent seeds.
        value=choose_scale(1000,{8:100,16:600,32:700},10,fixed_s=0,safety=1)
        self.assertEqual((value['quality_seed_count'],value['speed_generations']),(80,8))
        self.assertEqual(value['quality_seeds'],list(range(25,105)))
        self.assertEqual(value['speed_repetitions'],2)
        self.assertEqual(value['quality_generations'],64)

    def test_global_scale_reduces_before_opening_fresh_data(self):
        value=choose_scale(550,{8:100,16:200,32:300},10,fixed_s=0,safety=1)
        self.assertEqual((value['quality_seed_count'],value['speed_generations']),(40,8))
        self.assertEqual(value['estimated_total_s'],500)
        self.assertEqual(choose_scale(200,{8:100,16:200,32:300},10,0,1)['quality_seed_count'],10)

    def test_insufficient_budget_does_not_drop_tasks_or_minimum_seeds(self):
        self.assertIsNone(choose_scale(199,{8:100,16:200,32:300},10,0,1))
        self.assertIsNone(choose_scale(-1,{8:100,16:200,32:300},10,0,1))

    def test_safety_and_fixed_cost_are_paid(self):
        value=choose_scale(10000,{8:100,16:200,32:300},10,600,1.35)
        self.assertEqual(value['estimated_total_s'],600+1.35*(300+80*10))

    def test_invalid_or_partial_forecasts_fail_closed(self):
        for costs in ({8:1},{8:0,16:1,32:1},{8:1,16:float('nan'),32:1}):
            with self.assertRaises(ValueError):choose_scale(10000,costs,10)
        with self.assertRaises(ValueError):choose_scale(10000,{8:1,16:1,32:1},0)


if __name__=='__main__':unittest.main()
