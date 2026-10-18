import unittest
import analysis


class TestPairedInference(unittest.TestCase):
    def test_exact_discordance_test_and_symmetry(self):
        self.assertEqual(analysis.mcnemar(0,0),1)
        self.assertEqual(analysis.mcnemar(5,0),.0625)
        self.assertEqual(analysis.mcnemar(5,1),analysis.mcnemar(1,5))

    def test_holm_preserves_original_order_and_monotonic_adjustment(self):
        self.assertEqual(analysis.holm([.04,.01,.03]),[.06,.03,.06])

    def test_wilson_includes_nonzero_uncertainty_at_extremes(self):
        self.assertGreater(analysis.wilson(0,20)[1],.1)
        self.assertLess(analysis.wilson(20,20)[0],.9)
        self.assertEqual(analysis.wilson(0,0),(None,None))

    def test_paired_delta_zero_has_zero_interval(self):
        self.assertEqual(analysis.bootstrap([0]*20,repetitions=50),(0,0))


if __name__=='__main__':unittest.main()
