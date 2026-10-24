import copy
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import secondary
import serial


class FakeStage:
    def __init__(self):
        self.m = dict(jobs={})

    def persist(self):
        pass


class TestSecondaryControls(unittest.TestCase):
    def fixture(self, mode="gpu_overlap"):
        population = 16384
        data = dict(mode=mode, population=population, cases=200, evolve_wall_ms=1, final_best=0,
                    final_population=[dict(nodes=1)] * population,
                    generations=[dict(expected_program_cases=population * 200, cases=population * 200,
                                      unscored=0, repro_kernel_ms=1)] * 64)
        if mode == "asgp_1t":
            data["reproduction_profile"] = "asgp-fixed-native-elite1"
        else:
            data.update(reproduction_profile="native-gpu-phase-v2-counted", reproduction_fallback_reason="",
                        verification_top_k=0, source_depth_cap=0, gpu_phase_site_depth=0, gpu_phase_site_parsimony=0,
                        gpu_blocksize=512, cpu_audit_reduction_lanes=512,
                        cpu_audit_program_count=16, cpu_audit_wrong_program_count=16,
                        cpu_audit_mismatches=0, cpu_audit_wrong_mismatches=0)
        return data

    def test_p16384_and_g64_are_not_replaced_by_primary_or_short_runs(self):
        for mode in ("gpu_overlap", "asgp_1t"):
            data = self.fixture(mode)
            secondary.validate(data, mode, 200)
            for field, value in (("population", 1024), ("cases", 1024)):
                bad = copy.deepcopy(data)
                bad[field] = value
                with self.assertRaises(AssertionError):
                    secondary.validate(bad, mode, 200)
            bad = copy.deepcopy(data)
            bad["generations"] = bad["generations"][:8]
            with self.assertRaises(AssertionError):
                secondary.validate(bad, mode, 200)

    def test_cpu_reproduction_or_cpu_verification_mismatch_is_rejected(self):
        for field, value in (("reproduction_profile", "cpu"), ("reproduction_fallback_reason", "unsupported"),
                             ("mode", "cpu_1t"), ("cpu_audit_wrong_mismatches", 1),
                             ("verification_top_k", 8), ("gpu_blocksize", 128)):
            data = self.fixture()
            data[field] = value
            with self.assertRaises(AssertionError):
                secondary.validate(data, "gpu_overlap", 200)

    def test_unadmitted_initial_population_is_kept_and_not_resampled(self):
        stage = FakeStage()
        entry = dict(status="native_unadmitted", source="retained-original")
        freeze = dict(seed=35, variants={"candidate": dict(mode="gpu_overlap", extra={})})
        result = secondary.search(stage, "fixture", entry, freeze, "candidate")
        self.assertEqual(result["status"], "not_run_input_preparation_failure")
        self.assertEqual(result["preparation_status"], "native_unadmitted")
        self.assertEqual(result["population"], 16384)
        self.assertEqual(len(stage.m["jobs"]), 1)

    def test_different_cross_population_prefix_retains_one_shared_p16384_draw(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            primary_source = folder / "primary.json"
            grammar = folder / "grammar.json"
            grammar.write_text("unit fixture")
            serial.save(primary_source, dict(population=["old"] * 1024, cases=[dict(inputs=[1], expected=1)]))
            calls = []
            stage = FakeStage()
            stage.out = folder

            def run(tag, command, *args, **kwargs):
                calls.append(command[0])
                if command[0] == "freeze":
                    serial.save(command[2], dict(task="fixture", population_seed=35, population=["new"] * 16384))
                else:
                    self.assertEqual(command[0], "prepare")
                    serial.save(command[4], dict(programs=["native fixture"] * 16384))
                return dict(status="ok")

            stage.run = run
            primary = dict(status="prepared", seeds={"35": dict(status="ok", source=str(primary_source),
                           source_sha256=serial.sha(primary_source))}, grammar=str(grammar),
                           grammar_sha256=serial.sha(grammar), metadata=dict(train_count=1))
            freeze = dict(seed=35, reserve_s=14400, data_seed=20261024)
            with mock.patch.object(serial, "GPU_LOCK", folder / "lock"), \
                    mock.patch.object(secondary.time, "time", return_value=serial.START + 100):
                result = secondary.prepare_task(stage, "fixture", primary, freeze)
            self.assertEqual(result["status"], "ok")
            self.assertFalse(result["primary_prefix_equal"])
            self.assertEqual(calls, ["freeze", "prepare"])
            self.assertEqual(serial.read(result["source"])["population"], ["new"] * 16384)
            self.assertEqual(serial.read(result["source"])["cases"], serial.read(primary_source)["cases"])


if __name__ == "__main__":
    unittest.main()
