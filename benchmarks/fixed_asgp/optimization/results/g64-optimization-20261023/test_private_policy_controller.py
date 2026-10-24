import unittest

import private_layout_pilot5c


class TestGlobalPrivatePolicies(unittest.TestCase):
    def test_policy_and_compiler_ablations_are_complete_global_recipes(self):
        parent = dict(GAGP_PHASE_REPRO_THREADS="4", GAGP_GPU_VIEW_PHYSICAL_BLOCK="128")
        arms = private_layout_pilot5c.recipes(parent)
        self.assertEqual(set(arms), {"baseline", "ordinary", "control32", "hybrid", "specialized_parent", "combined"})
        self.assertEqual(arms["baseline"], {})
        for name, config in arms.items():
            if name == "baseline":
                continue
            self.assertEqual(config["GAGP_PHASE_REPRO_THREADS"], "4")
            self.assertFalse(any("CASE_COUNT" in key or "WORKSPACE" in key or "GENERATIONS" in key for key in config))
        self.assertEqual(arms["combined"]["GAGP_GPU_DENSE_CASE_TILES"], "tuple")
        self.assertEqual(arms["combined"]["GAGP_GPU_CASE_VIEW_BLOCK"], "hybrid")
        self.assertEqual(arms["combined"]["GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT"], "1")
        self.assertEqual(arms["hybrid"]["GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT"], "0")
        self.assertNotIn("GAGP_GPU_DENSE_CASE_TILES", arms["specialized_parent"])
        self.assertEqual(parent, dict(GAGP_PHASE_REPRO_THREADS="4", GAGP_GPU_VIEW_PHYSICAL_BLOCK="128"))


if __name__ == "__main__":
    unittest.main()
