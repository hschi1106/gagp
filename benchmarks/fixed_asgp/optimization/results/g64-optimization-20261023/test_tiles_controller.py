import importlib
import unittest
from unittest import mock

import serial
import tiles_pilot4


class TestTilesRecipes(unittest.TestCase):
    def test_tiled_candidate_keeps_auto_admission_and_logical_resources(self):
        existing = {"GAGP_GPU_VIEW_PHYSICAL_BLOCK": "128", "GAGP_GPU_RELAXED_VIEW_REGISTERS": "1"}
        arms = tiles_pilot4.recipes(existing, "auto")
        self.assertEqual(arms["baseline"], {})
        self.assertEqual(arms["ordinary"]["GAGP_GPU_DENSE_CASE_TILES"], "0")
        self.assertEqual(arms["untiled_dense"]["GAGP_GPU_TILED_INITIAL_PAYLOAD"], "0")
        self.assertEqual(arms["dense_tiles"]["GAGP_GPU_TILED_INITIAL_PAYLOAD"], "auto")
        self.assertEqual(arms["dense_tiles_view"]["GAGP_GPU_VIEW_PHYSICAL_BLOCK"], "0")
        self.assertEqual(arms["dense_tiles_view"]["GAGP_GPU_CASE_VIEW_BLOCK"], "1")
        self.assertNotIn("GAGP_GPU_VIEW_BOUND128", arms["dense_tiles_view"])
        self.assertEqual(arms["dense_tiles_view128"]["GAGP_GPU_VIEW_BOUND128"], "1")
        self.assertEqual(existing, {"GAGP_GPU_VIEW_PHYSICAL_BLOCK": "128", "GAGP_GPU_RELAXED_VIEW_REGISTERS": "1"})
        for config in arms.values():
            self.assertFalse(any("WORKSPACE" in key or "CASE_COUNT" in key or "GENERATIONS" in key for key in config))

    def test_import_cannot_start_or_mutate_experiment(self):
        with mock.patch.object(serial, "save", side_effect=AssertionError("Import starts work")):
            for module in ("tiles_pilot4", "apply_tiles4", "stage_dense_tiles4", "untiled_dense_pilot"):
                importlib.reload(importlib.import_module(module))


if __name__ == "__main__":
    unittest.main()
