from __future__ import annotations

import copy
import json
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from gagp_tools.reports.grammar_migration import compare_manifest, compare_row
from gagp_tools.experiments.grammar_migration import (
    MODES, extract_scopes, measurement_command, run_manifest, sha256, validate_manifest, workload_modes, workload_scopes,
)
from gagp_tools.reports.audit_migration_trials import audit_trials
from gagp_tools.experiments.freeze_migration_workloads import public_value


def blocks(count=15, cpu_after=100, mode_after=10):
    return [{"block_id": i, "warmup": i < 3, "cpu_before_ms": 100,
             "mode_before_ms": 10, "cpu_after_ms": cpu_after,
             "mode_after_ms": mode_after} for i in range(count + 3)]


class TestGrammarMigration(unittest.TestCase):
    def test_reproduction_modes_use_frozen_tape_and_separate_executables(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "frozen.json"
            source.write_text("{}")
            artifact = {"path": str(source), "sha256": sha256(source)}
            workload = {"id": "parents", "measurement": "steady_repro", "snapshot": artifact,
                "cases": artifact, "donor_tape": artifact, "session_warmups": 3, "session_trials": 15,
                "args": ["--population-size", "64", "--generations", "1", "--blocksize", "256",
                         "--seed", "42", "--fuel", "20000"]}
            manifest = {"version": "migration-workloads-v1", "warmup_blocks": 3, "measured_blocks": 15,
                        "analysis_seed": 42, "modes": list(MODES), "workloads": [workload]}
            validate_manifest(manifest, root)
            self.assertEqual(set(workload_modes(workload)), {"cpu", "gpu_repro"})
            self.assertEqual(workload_scopes(workload), ("steady_reproduction",))
            cpu = measurement_command(workload, "cpu", root / "gpu", root, root / "out", root / "cpu")
            gpu = measurement_command(workload, "gpu_repro", root / "gpu", root, root / "out")
            self.assertEqual(cpu[0], str(root / "cpu"))
            self.assertIn("--donor-tape", cpu)
            self.assertNotIn("--action", cpu)
            self.assertEqual(gpu[:3], [str(root / "gpu"), "--action", "repro-steady"])
            with self.assertRaises(ValueError):
                measurement_command(workload, "cpu", root / "gpu", root, root / "out")
            del workload["donor_tape"]
            with self.assertRaises(ValueError):
                validate_manifest(manifest, root)

    def test_measurement_command_uses_compiled_definition_flag(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            artifact = {"path": "artifact.json", "sha256": "unused"}
            workload = {
                "measurement": "evolution",
                "snapshot": artifact,
                "cases": artifact,
                "grammar": artifact,
                "args": ["--population-size", "64", "--generations", "1",
                         "--blocksize", "256", "--seed", "42", "--fuel", "20000"],
            }
            command = measurement_command(
                workload, "cpu", root / "adapter", root, root / "result.json")
            self.assertIn("--grammar-definition", command)
            self.assertNotIn("--grammar-config", command)

    def test_raw_audit_detects_pruned_rows_and_fabricated_timings(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            fixture = root / "fixture.json"
            fixture.write_text("{}")
            binary = root / "adapter"
            binary.write_text("test executable identity")
            artifact = {"path": str(fixture), "sha256": sha256(fixture)}
            manifest = {"version": "migration-workloads-v1", "warmup_blocks": 3,
                "measured_blocks": 15, "analysis_seed": 42, "modes": list(MODES),
                "workloads": [{"id": "scalar", "cases": artifact, "snapshot": artifact,
                    "measurement": "steady_eval", "session_warmups": 3, "session_trials": 15,
                    "args": ["--population-size", "64", "--generations", "1", "--blocksize", "256",
                             "--seed", "42", "--fuel", "20000"]}]}

            def execute(command, **kwargs):
                output = Path(command[command.index("--out-json") + 1])
                output.write_text(json.dumps({"format_version": "migration-steady-eval-v1",
                    "warmups": 3, "measured_trials": 15,
                    "samples": [{"index": i, "warmup": i < 3, "call_ms": 2} for i in range(18)]}))
                return SimpleNamespace(returncode=0, stdout="", stderr="")

            directory = root / "trials"
            with patch("gagp_tools.experiments.grammar_migration.gpu_state", return_value={}), \
                    patch("gagp_tools.experiments.grammar_migration.subprocess.run", side_effect=execute):
                report = run_manifest(manifest, root, binary, None, directory, 0)
            result = audit_trials(directory, manifest, root)
            self.assertEqual(result["status"], "complete")
            self.assertFalse(result["paired"])
            self.assertEqual(result["checked_processes"], 36)
            # A self-consistent pruned report must still fail the independent inventory.
            changed = copy.deepcopy(report)
            removed = changed["required_rows"].pop()
            del changed["rows"][removed]
            (directory / "trials.json").write_text(json.dumps(changed))
            with self.assertRaisesRegex(ValueError, "independently frozen"):
                audit_trials(directory, manifest, root)
            changed = copy.deepcopy(report)
            changed["rows"][changed["required_rows"][0]]["blocks"][3]["mode_before_ms"] = 1
            (directory / "trials.json").write_text(json.dumps(changed))
            with self.assertRaisesRegex(ValueError, "raw measurements"):
                audit_trials(directory, manifest, root)
            (directory / "trials.json").write_text(json.dumps(report, indent=2) + "\n")
            raw = next(directory.rglob("*.process.json"))
            raw.write_text(raw.read_text() + " ")
            with self.assertRaisesRegex(ValueError, "hash changed"):
                audit_trials(directory, manifest, root)

            # A failed process must not discard completed paired workload blocks.
            recovered_directory = root / "recover"
            calls = []

            def fail_once(command, **kwargs):
                calls.append(command)
                if len(calls) == 3:
                    return SimpleNamespace(returncode=1, stdout="", stderr="out of memory")
                return execute(command, **kwargs)

            with patch("gagp_tools.experiments.grammar_migration.gpu_state", return_value={}), \
                    patch("gagp_tools.experiments.grammar_migration.subprocess.run", side_effect=fail_once):
                with self.assertRaisesRegex(RuntimeError, "measurement failed"):
                    run_manifest(manifest, root, binary, None, recovered_directory, 0)
                completed = recovered_directory / "block-000/workload-000/before-cpu.process.json"
                completed_bytes = completed.read_bytes()
                run_manifest(manifest, root, binary, None, recovered_directory, 0, resume=True)
            self.assertEqual(len(calls), 37)
            self.assertEqual(completed.read_bytes(), completed_bytes)
            replacement = next(recovered_directory.glob("failed-attempts/**/replacement.json"))
            self.assertEqual(json.loads(replacement.read_text())["reason"], "failed_process")
            self.assertEqual(audit_trials(recovered_directory, manifest, root)["status"], "complete")

    def test_case_materialization_refuses_lossy_public_values(self):
        self.assertEqual(public_value({"tag": 0, "bits": "ffffffffffffffff", "bool": False}),
                         {"type": "int", "value": -1})
        self.assertEqual(public_value({"tag": 4, "bits": "0000000000000000", "bool": False,
                                      "bytes_hex": "616263"}), {"type": "string", "value": "abc"})
        for value in ({"tag": 0, "bits": "7fffffffffffffff"},
                      {"tag": 1, "bits": "7ff0000000000000"},
                      {"tag": 4, "bits": "0000000000000000", "materialized": False}):
            with self.assertRaises(ValueError):
                public_value(value)

    def test_steady_session_excludes_warmups_without_counting_calls_as_blocks(self):
        raw = {"format_version": "migration-steady-eval-v1", "warmups": 3, "measured_trials": 15,
               "samples": [{"index": i, "warmup": i < 3, "call_ms": 1000 if i < 3 else 2}
                           for i in range(18)]}
        self.assertEqual(extract_scopes(raw, 9999), {"steady_eval": 2})
        workload = {"measurement": "steady_eval"}
        self.assertEqual(set(workload_modes(workload)), {"cpu", "gpu_eval"})
        self.assertEqual(workload_scopes(workload), ("steady_eval",))
        raw["samples"][0]["warmup"] = False
        with self.assertRaises(ValueError):
            extract_scopes(raw, 9999)
        raw["samples"] = raw["samples"][:-1]
        with self.assertRaises(ValueError):
            extract_scopes(raw, 9999)

    def test_scope_extraction_does_not_sum_overlap(self):
        measured = extract_scopes({"format_version": "migration-run-v1", "gpu_eval_init_ms": 3,
            "evolve_call_ms": 15, "generations": [{"total_ms": 10, "eval_ms": 8, "repro_ms": 8}]}, 20)
        self.assertEqual(measured, {"cli_wall": 20, "canonical_cold": 13, "evolve_call": 15})

    def test_runner_checks_frozen_files_and_modes(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            fixture = root / "fixture.json"
            fixture.write_text("{}")
            artifact = {"path": "fixture.json", "sha256": sha256(fixture)}
            manifest = {"version": "migration-workloads-v1", "warmup_blocks": 3,
                "measured_blocks": 15, "analysis_seed": 42, "modes": list(MODES),
                "workloads": [{"id": "scalar", "cases": artifact, "snapshot": artifact,
                    "args": ["--population-size", "64", "--generations", "1", "--blocksize", "256",
                             "--seed", "42", "--fuel", "20000"]}]}
            validate_manifest(manifest, root)
            changed = copy.deepcopy(manifest)
            changed["modes"].remove("gpu_repro_overlap")
            with self.assertRaises(ValueError):
                validate_manifest(changed, root)
            changed = copy.deepcopy(manifest)
            changed["workloads"][0]["args"].extend(["--engine", "cpu"])
            with self.assertRaises(ValueError):
                validate_manifest(changed, root)
            fixture.write_text("changed")
            with self.assertRaises(ValueError):
                validate_manifest(manifest, root)

    def test_noisy_paired_blocks_remain_unresolved(self):
        data = blocks(30)
        for i, row in enumerate(data):
            row["mode_after_ms"] = 1 if i % 2 else 100
        result = compare_row(data, seed=42)
        self.assertNotEqual(result["status"], "pass")

    def test_reference_vs_reference(self):
        result = compare_row(blocks(), seed=42)
        self.assertEqual(result["status"], "pass")
        self.assertEqual(result["metrics"]["Q"]["ci95"], [1, 1])
        self.assertEqual(result["speedup_before"], 10)
        self.assertEqual(result["speedup_after"], 10)
        self.assertEqual(result["median_ms"]["cpu_before_ms"], 100)

    def test_symmetric_slowdown_cannot_hide_absolute_regression(self):
        result = compare_row(blocks(30, 200, 20), seed=42)
        self.assertEqual(result["metrics"]["Q"]["estimate"], 1)
        self.assertEqual(result["status"], "regression")

    def test_speedup_loss_even_when_both_absolute_times_improve(self):
        result = compare_row(blocks(30, 50, 9), seed=42)
        self.assertGreater(result["metrics"]["A_mode"]["estimate"], 1)
        self.assertEqual(result["status"], "regression")

    def test_requires_rerun_before_declaring_persistent_loss(self):
        self.assertEqual(compare_row(blocks(mode_after=11), seed=42)["status"], "rerun")

    def test_incomplete_trials(self):
        self.assertEqual(compare_row(blocks(14), seed=42)["status"], "pending")
        self.assertEqual(compare_row(blocks()[3:], seed=42)["status"], "pending")

    def test_invalid_trials(self):
        for update in ({"mode_after_ms": float("nan")}, {"excluded": True},
                       {"cpu_before_ms": 0}, {"cpu_after_ms": True}):
            data = blocks()
            data[3].update(update)
            with self.assertRaises(ValueError):
                compare_row(data, seed=42)
        data = blocks()
        data.append(data[0])
        with self.assertRaises(ValueError):
            compare_row(data, seed=42)

    def test_manifest_rejects_missing_rows_workload_change_and_phase_sums(self):
        manifest = {"version": "grammar-migration-trials-v1", "analysis_seed": 42,
                    "required_rows": ["scalar/gpu_eval/cold"], "rows": {
                        "scalar/gpu_eval/cold": {"workload_before_sha256": "abc",
                            "workload_after_sha256": "abc", "timing_source": "direct",
                            "blocks": blocks()}}}
        for field, value in (("workload_after_sha256", "different"),
                             ("timing_source", "phase_sum")):
            changed = copy.deepcopy(manifest)
            changed["rows"]["scalar/gpu_eval/cold"][field] = value
            with self.assertRaises(ValueError):
                compare_manifest(changed)
        manifest["required_rows"].append("scalar/gpu_repro/cold")
        with self.assertRaises(ValueError):
            compare_manifest(manifest)
