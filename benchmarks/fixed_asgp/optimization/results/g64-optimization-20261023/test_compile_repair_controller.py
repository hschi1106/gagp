import importlib
import unittest
from unittest import mock

import private_layout_pilot5c
import private_layout_pilot6
import serial


class TestCompileRepair(unittest.TestCase):
    def test_repair_does_not_change_the_predeclared_global_trial_recipes(self):
        parent = {"GAGP_PHASE_REPRO_THREADS": "1", "GAGP_GPU_VIEW_PHYSICAL_BLOCK": "128"}
        self.assertEqual(private_layout_pilot6.recipes(parent), private_layout_pilot5c.recipes(parent))

    def test_import_cannot_repair_source_or_start_build(self):
        with mock.patch.object(serial, "save", side_effect=AssertionError("Import cannot mutate work")):
            importlib.reload(importlib.import_module("apply_compile_repair6"))


if __name__ == "__main__":
    unittest.main()
