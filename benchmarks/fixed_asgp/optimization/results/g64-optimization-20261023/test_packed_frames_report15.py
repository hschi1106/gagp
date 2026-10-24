"""Negative evidence guards for the isolated development appendix."""
from pathlib import Path
import tempfile
import unittest

from packed_frames_report15 import NATIVE_TESTS, full_mean, native_tests_passed, verify_identity, verify_inputs
from appendix_audit18 import physical_savings_valid
import serial


class TestPackedAppendixEvidence(unittest.TestCase):
    def test_shape_cache_makes_physical_saving_smaller_than_logical_group_cases(self):
        self.assertTrue(physical_savings_valid(129350, 127800, 22, 200))
        self.assertTrue(physical_savings_valid(204800, 195200, 48, 200))
        self.assertFalse(physical_savings_valid(129350, 129351, 22, 200))
        self.assertFalse(physical_savings_valid(129350, 120000, 22, 200))

    def test_missing_or_invalid_task_cannot_form_full_mean(self):
        self.assertIsNone(full_mean({}, []))
        self.assertIsNone(full_mean({"a": 2}, ["a", "b"]))
        self.assertIsNone(full_mean({"a": 2, "b": None}, ["a", "b"]))
        self.assertIsNone(full_mean({"a": 2, "b": 0}, ["a", "b"]))
        self.assertIsNone(full_mean({"a": 2, "b": float("inf")}, ["a", "b"]))
        self.assertAlmostEqual(full_mean({"a": 2, "b": .5}, ["a", "b"]), 1)

    def test_exit_zero_or_ctest_no_tests_is_not_six_native_passes(self):
        self.assertFalse(native_tests_passed("No tests were found!!!"))
        self.assertFalse(native_tests_passed("100% tests passed, 0 tests failed out of 6"))
        lines = [f"{i}/6 Test #{i}: {name} .... Passed 0.1 sec" for i, name in enumerate(sorted(NATIVE_TESTS), 1)]
        log = "\n".join(lines + ["100% tests passed, 0 tests failed out of 6"])
        self.assertTrue(native_tests_passed(log))
        self.assertFalse(native_tests_passed(log.replace(sorted(NATIVE_TESTS)[0], "another_test")))

    def test_intact_file_from_another_arm_is_still_wrong_binary(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "bench"
            binary.write_bytes(b"another intact arm")
            stage = {"binary_sha256": serial.sha(binary)}
            with self.assertRaisesRegex(ValueError, "Wrong binary"):
                verify_identity(stage, binary, "1" * 64)

    def test_changed_fixed_input_and_wrong_global_recipe_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            spec = {k: str(Path(folder) / k) for k in ("source", "prepared", "grammar")}
            for path in spec.values():
                Path(path).write_text(path)
            job = dict(input_sha256={p: serial.sha(p) for p in spec.values()}, extra={"packed": "aos"})
            verify_inputs(job, spec, {"packed": "aos"})
            with self.assertRaisesRegex(ValueError, "global variant"):
                verify_inputs(job, spec, {"packed": "soa"})
            Path(spec["source"]).write_text("changed draw")
            with self.assertRaisesRegex(ValueError, "fixed development"):
                verify_inputs(job, spec, {"packed": "aos"})


if __name__ == "__main__":
    unittest.main()
