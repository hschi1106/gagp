import unittest
import layout_score_followup8


class TestLayoutScoringArms(unittest.TestCase):
    def test_global_arms_preserve_parent_and_only_change_declared_execution_flags(self):
        parent = {"GAGP_GPU_CASE_VIEW_BLOCK": "hybrid", "GAGP_GPU_DENSE_CASE_TILES": "tuple",
                  "GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT": "0"}
        arms = layout_score_followup8.recipes(parent)
        self.assertEqual(parent["GAGP_GPU_DENSE_CASE_TILES"], "tuple")
        self.assertEqual(arms["ordinary"], parent)
        allowed = {"GAGP_GPU_PRIVATE_CELL_SOA", "GAGP_GPU_SHORT_EDIT_DISTANCE", "GAGP_GPU_DENSE_CASE_TILES"}
        for name, arm in arms.items():
            if name == "baseline":
                continue
            self.assertLessEqual({k for k in arm if arm[k] != parent.get(k)}, allowed)
            self.assertEqual(arm["GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT"], "0")
            self.assertEqual(arm["GAGP_GPU_CASE_VIEW_BLOCK"], "hybrid")
        self.assertEqual(arms["soa_short_dense"]["GAGP_GPU_DENSE_CASE_TILES"], "1")

    def test_layout_and_scorer_have_separate_controls(self):
        arms = layout_score_followup8.recipes({})
        self.assertEqual(arms["soa"], {"GAGP_GPU_PRIVATE_CELL_SOA": "1"})
        self.assertEqual(arms["short"], {"GAGP_GPU_SHORT_EDIT_DISTANCE": "1"})
        self.assertEqual(arms["soa_short"]["GAGP_GPU_SHORT_EDIT_DISTANCE"], "1")

    def test_uses_existing_eight_task_development_set(self):
        tasks = layout_score_followup8.development_v2.SCREEN_TASKS
        self.assertEqual(len(tasks), 8)
        self.assertEqual(len(set(tasks)), 8)
