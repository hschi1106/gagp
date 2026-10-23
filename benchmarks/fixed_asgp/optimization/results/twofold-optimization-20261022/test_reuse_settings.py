import unittest

import reuse_pilot


class TestExactReuseExecutionScope(unittest.TestCase):
    def test_baseline_and_disabled_reuse_are_explicit(self):
        for selected in ('basic','full'):
            for physical in (0,128,256):
                baseline=reuse_pilot.execution_settings('baseline',selected,physical)
                self.assertTrue(all(value=='0' for value in baseline.values()))
                before=reuse_pilot.execution_settings('before',selected,physical)
                reused=reuse_pilot.execution_settings('reuse',selected,physical)
                self.assertEqual({key for key in before if before[key]!=reused[key]},
                                 {'GAGP_GPU_UNCHANGED_FITNESS_REUSE'})
                self.assertEqual((before['GAGP_GPU_UNCHANGED_FITNESS_REUSE'],reused['GAGP_GPU_UNCHANGED_FITNESS_REUSE']),('0','1'))

    def test_unfrozen_variant_or_physical_width_is_rejected(self):
        for args in [('other','basic',128),('reuse','other',128),('reuse','full',64)]:
            with self.assertRaises(ValueError):reuse_pilot.execution_settings(*args)


if __name__=='__main__':unittest.main()
