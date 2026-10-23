import unittest

import projection_simple_pilot


class TestProjectionSimpleSettings(unittest.TestCase):
    def test_factorial_arms_only_change_the_named_flags(self):
        helper=projection_simple_pilot.execution_settings;base=helper('shape')
        for variant,expected in [('projected',{'GAGP_GPU_PROJECT_SHAPE_INPUTS'}),
                                 ('simple',{'GAGP_GPU_SIMPLE_PHASES'}),
                                 ('projected_simple',{'GAGP_GPU_PROJECT_SHAPE_INPUTS','GAGP_GPU_SIMPLE_PHASES'})]:
            current=helper(variant)
            self.assertEqual(set(current),set(base))
            self.assertEqual({k for k in base if current[k]!=base[k]},expected)
            self.assertEqual(current['GAGP_GPU_CUDA_POOL'],'0')
            self.assertEqual(current['GAGP_GPU_VIEW_PHYSICAL_BLOCK'],'0')
            self.assertEqual(current['GAGP_GPU_REGISTER_PHASE_STACK'],'0')
        with self.assertRaises(ValueError):helper('per_task_winner')


if __name__=='__main__':unittest.main()
