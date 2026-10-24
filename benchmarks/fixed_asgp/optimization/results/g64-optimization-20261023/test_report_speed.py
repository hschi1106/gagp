import tempfile
from pathlib import Path
import unittest
from unittest import mock

import report_evidence
import report_speed
import serial


class TestClosedSpeedEvidence(unittest.TestCase):
    def test_repetition_identity_ignores_elapsed_time_but_checks_selected_program(self):
        a = dict(history=[dict(ast="a", fitness=2, generation=0, elapsed_search_ms=10)])
        b = dict(history=[dict(ast="a", fitness=2, generation=0, elapsed_search_ms=20)])
        self.assertEqual(report_speed.selected_trajectory(a, "baseline"), report_speed.selected_trajectory(b, "baseline"))
        b["history"][0]["ast"] = "different"
        self.assertNotEqual(report_speed.selected_trajectory(a, "baseline"), report_speed.selected_trajectory(b, "baseline"))

    def test_absent_phase_is_a_complete_grid_of_missing_records(self):
        freeze = dict(population=1024, repetitions=3, tasks=["a", "b"], seed=35,
                      variants={v: {} for v in ("baseline", "candidate", "asgp")})
        with tempfile.TemporaryDirectory() as folder, mock.patch.object(serial, "RAW", Path(folder)):
            rows, jobs, successes = report_speed.collect("none", "none-audits", freeze, {}, report_evidence.Evidence())
        self.assertEqual(len(rows), 6)
        self.assertEqual(len(jobs), 18)
        self.assertEqual(successes, [])
        self.assertTrue(all(row["statuses"] == ["missing", "missing"] and row["speedup"] is None for row in rows))
        self.assertTrue(all(not job["qualified"] for job in jobs))

    def test_secondary_uses_recorded_search_and_audit_names(self):
        freeze = dict(population=16384, tasks=["a"], seed=35, variants={v: {} for v in ("baseline", "candidate", "asgp")})
        reader = report_evidence.Evidence()
        with mock.patch.object(reader, "stage", return_value={}) as stage:
            rows, jobs, _ = report_speed.collect("secondary0", "ignored", freeze, {}, reader)
        self.assertEqual(len(rows), 1)
        self.assertEqual(len(jobs), 3)
        paths = [str(call.args[0]) for call in stage.call_args_list]
        self.assertTrue(any(path.endswith("secondary0-candidate-audits/manifest.json") for path in paths))


if __name__ == "__main__":
    unittest.main()
