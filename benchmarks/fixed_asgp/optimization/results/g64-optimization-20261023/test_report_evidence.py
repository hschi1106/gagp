import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import report_evidence


class TestImmutableEvidence(unittest.TestCase):
    def test_artifact_bytes_must_match_recorded_hash(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "artifact.json"
            path.write_text('{"value":1}')
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            reader = report_evidence.Evidence()
            self.assertEqual(reader.artifact(dict(output=str(path), output_sha256=digest)), {"value": 1})
            with self.assertRaises(AssertionError):
                reader.verify(path, "0" * 64)
            path.write_text('{"value":2}')
            with self.assertRaises(AssertionError):
                report_evidence.Evidence().verify(path, digest)

    def test_unknown_quality_pair_is_retained_without_ratio(self):
        a = dict(task="a", seed=35, complete=False, status="budget_not_run", final_success=False)
        b = dict(a, status="missing")
        row = report_evidence.quality_pair(a, b)
        self.assertFalse(row["complete_pair"])
        self.assertFalse(row["trajectory_equal"])
        self.assertNotIn("speedup", row)
        self.assertEqual(row["baseline_status"], "budget_not_run")

    def test_matching_success_does_not_hide_a_trajectory_difference(self):
        a = dict(task="a", seed=35, complete=True, status="ok", final_success=True, search_s=2,
                 champion_ast_fitness=[("initial", 0)], final_descriptors=[1], final_export_sha256="x",
                 train_cases_sha256="t", test_cases_sha256="u")
        b = dict(a, search_s=1, champion_ast_fitness=[("changed", 0)])
        row = report_evidence.quality_pair(a, b)
        self.assertTrue(row["complete_pair"])
        self.assertTrue(row["baseline_success"] and row["candidate_success"])
        self.assertFalse(row["trajectory_equal"])
        self.assertEqual(row["speedup"], 2)


if __name__ == "__main__":
    unittest.main()
