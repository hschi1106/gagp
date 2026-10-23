"""Admission and whole-scope pairing remain explicit for the new storage route."""
import ast
from pathlib import Path
import unittest
import private_adjacent_pilot as pilot

class TestAdjacentSettings(unittest.TestCase):

    def test_only_global_flag_changes(self):
        off = pilot.execution_settings('baseline')
        self.assertEqual(off, pilot.execution_settings('ordinary'))
        self.assertEqual(dict(off, GAGP_GPU_PRIVATE_ADJACENT_CONCAT='1'), pilot.execution_settings('adjacent'))

    def test_no_relaxed_limit_or_conflicting_route(self):
        for arm in pilot.VARIANTS:
            settings = pilot.execution_settings(arm)
            for flag in ('GAGP_GPU_PRIVATE_CELL_SOA', 'GAGP_GPU_SHORT_EDIT_DISTANCE', 'GAGP_FUSED_COLD_EXECUTABLE'):
                self.assertEqual(settings[flag], '0')
            self.assertEqual(settings['GAGP_PHASE_REPRO_THREADS'], '1')

    def test_unknown_arm_rejected(self):
        with self.assertRaises(ValueError):
            pilot.execution_settings('task-tuned')

    def test_combined_measurement_body_unchanged(self):
        root = Path(__file__).resolve().parent
        old = (root / 'combined2_pilot.py').read_text().replace('combined2', 'combined6')
        actual = (root / 'combined6_pilot.py').read_text()
        self.assertEqual(ast.dump(ast.parse(old)), ast.dump(ast.parse(actual)))
