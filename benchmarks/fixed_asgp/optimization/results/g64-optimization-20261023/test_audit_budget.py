from pathlib import Path
import tempfile
import unittest
from unittest import mock

import budgeted_audits
import full_population_audit_timed
import serial


class FakeStage:
    def __init__(self):
        self.m = dict(jobs={})

    def persist(self):
        pass


class TestAuditReserve(unittest.TestCase):
    def fixture(self):
        return dict(status="ok", task="fixture", population=16384, seed=35,
                    repetition=0, variant="candidate"), dict(reserve_s=14400)

    def test_cutoff_records_every_kind_without_opening_missing_input_files(self):
        job, freeze = self.fixture()
        with tempfile.TemporaryDirectory() as folder, mock.patch.object(serial, "GPU_LOCK", Path(folder) / "lock"), \
                mock.patch.object(budgeted_audits.time, "time", return_value=serial.DEADLINE - 14400):
            for function, args in ((budgeted_audits.run, ("train", None, freeze)),
                                   (budgeted_audits.asgp, ("test", None, freeze)),
                                   (budgeted_audits.common, ("train", None, freeze)),
                                   (budgeted_audits.full, (None, freeze))):
                stage = FakeStage()
                value = function(stage, job, *args)
                self.assertEqual(value["status"], "budget_not_run")
                self.assertTrue(value["inputs_not_read"])
                self.assertEqual(len(stage.m["jobs"]), 1)

    def test_full_export_parsing_is_guarded_again_after_lock_wait(self):
        job, freeze = self.fixture()
        stage = FakeStage()
        with tempfile.TemporaryDirectory() as folder, mock.patch.object(serial, "GPU_LOCK", Path(folder) / "lock"), \
                mock.patch.object(full_population_audit_timed.time, "time", return_value=serial.DEADLINE - 14400), \
                mock.patch.object(serial, "sha", side_effect=AssertionError("Must not read ASTs after lock wait")):
            value = full_population_audit_timed.run(stage, job, None, freeze)
        self.assertEqual(value["status"], "budget_not_run")
        self.assertTrue(value["inputs_not_read"])

    def test_existing_records_are_not_silently_replaced_at_cutoff(self):
        job, freeze = self.fixture()
        with tempfile.TemporaryDirectory() as folder, mock.patch.object(serial, "GPU_LOCK", Path(folder) / "lock"), \
                mock.patch.object(budgeted_audits.time, "time", return_value=serial.DEADLINE - 14400):
            stage = FakeStage()
            budgeted_audits.full(stage, job, None, freeze)
            with self.assertRaises(AssertionError):
                budgeted_audits.full(stage, job, None, freeze)


if __name__ == "__main__":
    unittest.main()
