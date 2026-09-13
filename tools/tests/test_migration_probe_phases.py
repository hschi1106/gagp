import copy
import unittest

from gagp_tools.experiments.capture_gpu_oracle import validate_probe_phases


class TestMigrationProbePhases(unittest.TestCase):
    def test_missing_boundary_probe_is_not_silently_ignored(self):
        row = {"probe_cap": 100, "first_non_timeout_fuel": 3,
               **{key: {"ordinary_core_match": True} for key in (
                   "result", "at_probe_cap", "at_boundary", "below_boundary")}}
        validate_probe_phases(row, gpu=True)
        for key in ("result", "at_probe_cap", "at_boundary", "below_boundary"):
            broken = copy.deepcopy(row)
            del broken[key]
            with self.assertRaises(ValueError):
                validate_probe_phases(broken, gpu=True)

    def test_nonterminating_probe_has_no_fabricated_boundary(self):
        row = {"probe_cap": 100, "first_non_timeout_fuel": None,
               "result": {}, "at_probe_cap": {}}
        validate_probe_phases(row, gpu=False)
        row["at_boundary"] = {}
        with self.assertRaises(ValueError):
            validate_probe_phases(row, gpu=False)


if __name__ == "__main__":
    unittest.main()
