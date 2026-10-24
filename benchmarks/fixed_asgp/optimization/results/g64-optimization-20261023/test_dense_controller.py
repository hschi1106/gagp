import copy
import importlib
import unittest
from unittest import mock

import dense_pilot
import serial


class TestDenseController(unittest.TestCase):
    def fixture(self):
        return (dict(status="requested_phase_complete", task_count=30,
                     geometric_mean=1.04, candidate_extra={"whole_recipe": "combined"}),
                dict(status="requested_phase_complete", full30_geometric_means={"candidate": 1.06},
                     selected_extra={"whole_recipe": "phase"}, coarse_geometric_means={"other": 2.0}))

    def test_selects_one_complete_global_recipe_without_multiplying_gains(self):
        combined, phase = self.fixture()
        value, source, extra = dense_pilot.select_recipe(combined, phase)
        self.assertEqual((value, source, extra), (1.06, "phase1", {"whole_recipe": "phase"}))
        phase.pop("full30_geometric_means")
        self.assertEqual(dense_pilot.select_recipe(combined, phase)[1], "combined4")

    def test_incomplete_or_invalid_development_is_rejected(self):
        combined, phase = self.fixture()
        for key, value in (("status", "stopped"), ("task_count", 8), ("geometric_mean", float("nan"))):
            bad = copy.deepcopy(combined)
            bad[key] = value
            with self.assertRaises(AssertionError):
                dense_pilot.select_recipe(bad, phase)

    def test_new_controller_imports_do_not_launch_jobs(self):
        with mock.patch.object(serial, "save", side_effect=AssertionError("Import must not start experiments")):
            for name in ("stage_dense_candidate", "build_candidate_v2", "apply_dense_candidate", "profile_g64", "full_audit_cost"):
                importlib.reload(importlib.import_module(name))


if __name__ == "__main__":
    unittest.main()
