"""Regression: a successful best-only export cannot certify full population ASTs."""
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import development_v2
import serial


class FakeStage:
    def __init__(self, out, exported):
        self.out = out
        self.exported = exported
        self.job = {}
        self.extra = None

    def run(self, tag, arguments, extra, **kwargs):
        self.extra = extra
        serial.save(arguments[-1], dict(
            population=1024, cases=1, generations=[dict(cases=1024, expected_program_cases=1024,
            unscored=0, repro_kernel_ms=1)] * 64,
            final_population=[dict(nodes=1)] * 1024,
            reproduction_profile="native-gpu-phase-fixture", reproduction_fallback_reason="",
            verification_top_k=0, source_depth_cap=0, gpu_phase_site_depth=0,
            gpu_phase_site_parsimony=0, gpu_blocksize=512, cpu_audit_reduction_lanes=512,
            cpu_audit_program_count=16, cpu_audit_wrong_program_count=16,
            cpu_audit_mismatches=0, cpu_audit_wrong_mismatches=0, evolve_wall_ms=1,
            initial_admission_ms=0, gpu_init_ms=0, final_eval_ms=0))
        serial.save(extra["GAGP_SEARCH_EXPORT"], dict(programs=[{}] * self.exported))
        serial.save(extra["GAGP_SEARCH_HISTORY_EXPORT"], dict(history=[]))
        self.job.update(status="ok")
        return self.job

    def persist(self):
        pass


class TestExportScope(unittest.TestCase):
    def search(self, count):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            stage = FakeStage(out, count)
            spec = dict(train_count=1, source="source", prepared="prepared", grammar="grammar")
            # The enclosing test runner holds the real study lock; isolate only
            # the mock stage's inner file lock to avoid self-deadlock.
            with mock.patch.object(serial, "GPU_LOCK", out / "mock.lock"):
                if count == 1024:
                    development_v2.record_search(stage, "fixture", "candidate", spec, {})
                    self.assertEqual(stage.job["exported_program_count"], 1024)
                    self.assertEqual(stage.job["status"], "ok")
                else:
                    with self.assertRaisesRegex(RuntimeError, "invalid_output"):
                        development_v2.record_search(stage, "fixture", "candidate", spec, {})
                    self.assertEqual(stage.job["status"], "invalid_output")
                    self.assertIn("Full export requires every final AST", stage.job["error"])
                    self.assertTrue(Path(stage.extra["GAGP_SEARCH_EXPORT"]).exists())
            self.assertNotIn("GAGP_SEARCH_EXPORT_BEST_ONLY", stage.extra)

    def test_successful_best_only_export_is_preserved_and_rejected(self):
        self.search(1)

    def test_full_export_requires_absent_presence_switch(self):
        self.search(1024)


if __name__ == "__main__":
    unittest.main()
