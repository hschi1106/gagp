from pathlib import Path
import json
import tempfile
import unittest

import development_limits2 as development_limits
import test_development_limits as fixtures


class TestDevelopmentLimitsCaption(unittest.TestCase):
    def test_caption_uses_actual_candidate_and_case_range(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path, tasks = fixtures.TestDevelopmentLimits().fixture(root)
            observation = json.loads(path.read_text())
            observation['candidate_family'] = 'combined7'
            path.write_text(json.dumps(observation))
            evidence = development_limits.checked_training_observation(path, root, tasks)
            caption = development_limits.limitation_text(evidence)
            self.assertIn('combined7', caption)
            self.assertIn('19–48', caption)
            self.assertNotIn('combined2', caption)
