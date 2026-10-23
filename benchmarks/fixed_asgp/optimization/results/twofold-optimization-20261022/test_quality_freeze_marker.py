import ast
from pathlib import Path
import unittest

import quality_data


class TestQualityFreezeMarker(unittest.TestCase):
    def test_corrected_builder_matches_actual_preparation_guard(self):
        root = Path(__file__).resolve().parent
        tree = ast.parse((root / 'best_effort_validation1.py').read_text())
        markers = [item.value.value for node in ast.walk(tree) if isinstance(node, ast.Call)
                   for item in node.keywords if item.arg == 'first_heldout_use'
                   and isinstance(item.value, ast.Constant)]
        self.assertEqual(markers, ['after immutable candidate/scale freeze'])
        class Stage:
            m = {}
            def persist(self):
                pass
        freeze = dict(first_heldout_use=markers[0], data_seed=20261022, population=1024, tasks=[])
        self.assertEqual(quality_data.prepare(Stage(), freeze), {})

    def test_original_wrong_literal_fails_before_stage_or_data_access(self):
        class Stage:
            def persist(self):
                raise RuntimeError('must not reach stage mutation')
        freeze = dict(first_heldout_use='after immutable best-effort candidate/full-grid freeze',
                      data_seed=20261022, population=1024, tasks=[])
        with self.assertRaises(AssertionError):
            quality_data.prepare(Stage(), freeze)
