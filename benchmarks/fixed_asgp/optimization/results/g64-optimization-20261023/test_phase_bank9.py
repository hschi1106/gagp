import unittest
import phase_bank_followup9


class TestPhaseBankArms(unittest.TestCase):
    def test_parent_and_logical_resources_are_preserved(self):
        parent = {"GAGP_GPU_CASE_VIEW_BLOCK": "hybrid", "GAGP_GPU_DENSE_CASE_TILES": "tuple",
                  "GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT": "0", "GAGP_GPU_PRIVATE_LAUNCH_BOUND": "32"}
        arms = phase_bank_followup9.recipes(parent)
        self.assertEqual(arms["ordinary"], parent)
        allowed = {"GAGP_GPU_REGISTER_PHASE_STACK", "GAGP_GPU_VIEW_BOUND128", "GAGP_GPU_SIMPLE_PHASES", "GAGP_GPU_PRIVATE_MEASURE_CACHE"}
        for name, arm in arms.items():
            if name == "baseline":
                continue
            self.assertLessEqual({k for k in arm if arm[k] != parent.get(k)}, allowed)
            self.assertEqual(arm["GAGP_GPU_DENSE_CASE_TILES"], "tuple")
            self.assertEqual(arm["GAGP_GPU_PRIVATE_LAUNCH_BOUND"], "32")
        for name in ("bank", "bank_narrow", "bank_simple", "bank_no_measure"):
            self.assertEqual(arms[name]["GAGP_GPU_REGISTER_PHASE_STACK"], "1")

    def test_narrow_compilation_and_phase_cache_have_separate_controls(self):
        arms = phase_bank_followup9.recipes({})
        self.assertEqual(arms["bank"], {"GAGP_GPU_REGISTER_PHASE_STACK": "1"})
        self.assertEqual(arms["bank_narrow"]["GAGP_GPU_VIEW_BOUND128"], "1")
        self.assertEqual(arms["bank_no_measure"]["GAGP_GPU_PRIVATE_MEASURE_CACHE"], "0")
