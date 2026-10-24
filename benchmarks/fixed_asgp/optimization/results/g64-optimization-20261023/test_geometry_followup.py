import unittest

import geometry_followup5


class TestWholeGeometryRecipe(unittest.TestCase):
    def test_each_candidate_preserves_the_whole_parent_recipe(self):
        parent = dict(GAGP_GPU_DENSE_CASE_TILES="1", GAGP_GPU_CASE_VIEW_BLOCK="1", GAGP_PHASE_REPRO_THREADS="1")
        result = geometry_followup5.recipes(parent)
        self.assertEqual(len(result), 17)
        self.assertEqual(result["baseline"], {})
        self.assertEqual(result["ordinary"], parent)
        for name, config in result.items():
            if name == "baseline":
                continue
            self.assertEqual(config["GAGP_GPU_DENSE_CASE_TILES"], "1")
            self.assertEqual(config["GAGP_GPU_CASE_VIEW_BLOCK"], "1")
            self.assertEqual(set(config) - set(parent), {"GAGP_GPU_PRIVATE_LAUNCH_BOUND"} if name.startswith("launch") else set())
        self.assertEqual(parent["GAGP_PHASE_REPRO_THREADS"], "1")


if __name__ == "__main__":
    unittest.main()
