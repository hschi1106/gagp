import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import development_limits


class TestDevelopmentLimits(unittest.TestCase):
    def fixture(self, root, changed=None):
        rows = [dict(task=str(i), population=1024, generations=64, cases=19+i,
                     statuses=['ok', 'ok'], search_s=[1., 2.], speedup=.5,
                     full_final_ast_equal=True, final_descriptors_equal=True,
                     champion_ast_fitness_equal=True) for i in range(30)]
        if changed:
            changed(rows)
        source = root / 'pairs.json'
        source.write_text(json.dumps(rows))
        observation = root / 'observation.json'
        observation.write_text(json.dumps(dict(source='pairs.json',
            source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
            observed_geometric_ratio=.5)))
        return observation, [str(i) for i in range(30)]

    def test_complete_workload_is_recomputed_and_keeps_qualification(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path, tasks = self.fixture(root)
            evidence = development_limits.checked_training_observation(path, root, tasks)
            self.assertAlmostEqual(evidence['checked_geometric_ratio'], .5)
            self.assertEqual(len(evidence['cells']), 30)
            self.assertIn('不同工作量', development_limits.limitation_text(evidence))

    def test_changed_source_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path, tasks = self.fixture(root)
            (root / 'pairs.json').write_text('[]')
            with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                development_limits.checked_training_observation(path, root, tasks)

    def test_cached_ratio_cannot_override_observed_times(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path, tasks = self.fixture(root, lambda rows: rows[0].update(speedup=2.))
            with self.assertRaisesRegex(ValueError, 'differs from search times'):
                development_limits.checked_training_observation(path, root, tasks)

    def test_partial_workload_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path, tasks = self.fixture(root, lambda rows: rows.pop())
            with self.assertRaisesRegex(ValueError, 'every task once'):
                development_limits.checked_training_observation(path, root, tasks)
