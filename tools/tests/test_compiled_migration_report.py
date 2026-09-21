from __future__ import annotations

import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from gagp_tools.experiments.compiled_migration import MODES, VARIATION_COUNTERS, run_manifest, sha256  # noqa: E402
from gagp_tools.reports.compiled_migration import build_report, main  # noqa: E402


def native_output(block: int, generations: int = 2) -> dict:
    rows = []
    for generation in range(generations):
        rows.append({
            "eval_ms": block + generation,
            "repro_ms": 2 + generation,
            "total_ms": 10 + block + generation,
            "evaluation": {
                "cpu_compile_ms": 0.2, "gpu_compile_ms": 0.3,
                "gpu_eval_call_ms": 0.4, "gpu_eval_pack_ms": 0.5,
                "gpu_eval_launch_prep_ms": 0.6, "gpu_eval_upload_ms": 0.7,
                "gpu_eval_kernel_ms": 0.8, "gpu_eval_copyback_ms": 0.9,
                "gpu_eval_teardown_ms": 1.0,
            },
            "reproduction": {
                "selection_ms": 0.1, "crossover_ms": 0.2, "mutation_ms": 0.3,
                "prepare_inputs_ms": 0.4, "setup_ms": 0.5, "preprocess_ms": 0.6,
                "pack_ms": 0.7, "upload_ms": 0.8, "kernel_ms": 0.9,
                "copyback_ms": 1.0, "decode_ms": 1.1, "teardown_ms": 1.2,
                "selection_kernel_ms": 0.25, "variation_kernel_ms": 0.65,
                "crossover_attempts": 2, "mutation_attempts": 3,
                "contract_rejections": 4, "budget_rejections": 5,
                "generation_rejections": 6, "acceptance_rejections": 7,
                "fallback_children": 1, "unchanged_children": 2,
                "changed_children": 3,
            },
            "best_fitness": 0.0, "mean_fitness": -1.0, "best_nodes": 3,
        })
    return {
        "format_version": "migration-run-v1",
        "load_ms": 0.25, "evolve_call_ms": 20 + block,
        "init_population_ms": 1.0, "gpu_eval_init_ms": 1.0,
        "final_eval_ms": 0.0, "total_ms": 30 + block,
        "generations": rows,
    }


class TestCompiledMigrationReport(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        artifacts = {}
        for name in ("generator", "grammar", "cases", "snapshot"):
            path = self.root / name
            path.write_text(name, encoding="utf-8")
            artifacts[name] = {"path": name, "sha256": sha256(path)}
        self.binary = self.root / "candidate"
        self.binary.write_text("candidate binary", encoding="utf-8")
        self.capture_index = 0
        self.manifest = {
            "version": "compiled-migration-workloads-v1",
            "status": "candidate preregistration; measurements pending",
            "warmup_blocks": 3, "measured_blocks": 15, "analysis_seed": 42,
            "modes": list(MODES), "generator": artifacts["generator"],
            "profiles": [], "generation_runs": [],
            "workloads": [{
                "id": "scalar-p3-g2-b256", "profile": "scalar", "domain": "int",
                "grammar": artifacts["grammar"], "cases": artifacts["cases"],
                "snapshot": artifacts["snapshot"],
                "args": ["--population-size", "3", "--generations", "2",
                         "--blocksize", "256", "--seed", "42", "--fuel", "20000",
                         "--penalty", "1", "--skip-final-eval", "on",
                         "--retain-final-population", "off"],
            }],
        }

    def tearDown(self) -> None:
        self.temporary.cleanup()

    @staticmethod
    def execute(command: list[str], **kwargs: object) -> SimpleNamespace:
        output = Path(command[command.index("--out-json") + 1])
        block = int(next(part.removeprefix("block-") for part in output.parts
                         if part.startswith("block-")))
        output.write_text(json.dumps(native_output(block)), encoding="utf-8")
        if "-o" in command:
            Path(command[command.index("-o") + 1]).write_text(
                f"Maximum resident set size (kbytes): {1000 + block}\n", encoding="utf-8")
        return SimpleNamespace(returncode=0, stdout="ok", stderr="")

    def capture(self, *, pilot: bool = False) -> Path:
        self.capture_index += 1
        output = self.root / f"{'pilot' if pilot else 'trials'}-{self.capture_index}"
        with patch("gagp_tools.experiments.compiled_migration.gpu_state", return_value={}), \
                patch("gagp_tools.experiments.compiled_migration.subprocess.run",
                      side_effect=self.execute):
            run_manifest(self.manifest, self.root, self.binary, output, 0,
                         block_limit=1 if pilot else None)
        return output

    def refresh_inventory(self, directory: Path) -> None:
        inventory_path = directory / "raw-sha256.json"
        inventory = {str(path.relative_to(directory)): sha256(path)
                     for path in sorted(directory.rglob("*"))
                     if path.is_file() and path != inventory_path}
        inventory_path.write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")

    def test_complete_report_uses_only_measured_blocks_and_correct_counter_rates(self) -> None:
        directory = self.capture()
        report = build_report(directory / "trials.json", self.manifest, self.root)
        self.assertTrue(report["complete"])
        self.assertEqual(report["status"], "candidate measurement summary complete")
        self.assertIsNone(report["comparison_or_acceptance_claim"])
        self.assertEqual(set(report["rows"]), {
            f"scalar-p3-g2-b256/{mode}" for mode in MODES})

        row = report["rows"]["scalar-p3-g2-b256/cpu"]
        measured = row["measured"]
        self.assertEqual(measured["blocks"], 15)
        self.assertEqual(measured["absolute_times_ms"]["canonical_cold"]["median"], 21.0)
        self.assertEqual(measured["absolute_times_ms"]["evolve_call"]["median"], 30.0)
        eval_phase = measured["phase_times_ms"]["generation_eval_ms"]
        self.assertEqual(eval_phase["by_generation"][0]["median"], 10.0)
        self.assertEqual(eval_phase["by_generation"][1]["median"], 11.0)
        self.assertEqual(eval_phase["all_generation_observations"]["count"], 30)
        variation = measured["variation"]
        self.assertEqual(variation["counter_totals"]["generation_repro_fallback_children"], 30)
        self.assertEqual(variation["counter_totals"]["generation_repro_unchanged_children"], 60)
        self.assertEqual(variation["counter_totals"]["generation_repro_changed_children"], 90)
        self.assertEqual(variation["classified_variation_outcome_denominator"], 150)
        self.assertEqual(variation["fallback_fraction_of_classified_variation_outcomes"], 0.2)
        self.assertEqual(variation["changed_fraction_of_classified_variation_outcomes"], 0.6)
        self.assertEqual(variation["retained_child_reference"]["per_block"], 6)
        self.assertEqual(variation["retained_child_reference"]["across_blocks"], 90)
        self.assertFalse(variation["retained_child_reference"]["is_variation_fraction_denominator"])
        self.assertEqual(measured["host_process_max_rss_kib"]["max"], 1017.0)
        self.assertEqual(measured["candidate_speedup_vs_cpu"]["canonical_cold"], 1.0)
        self.assertEqual(measured["candidate_speedup_vs_cpu"]["evolve_call"], 1.0)
        self.assertIn("process_wall", measured["candidate_speedup_vs_cpu"])
        self.assertNotIn("gpu_memory", json.dumps(report).lower())
        self.assertEqual(row["warmup_descriptive"]["blocks"], 3)

    def test_pilot_is_incomplete_and_warmup_is_descriptive_only(self) -> None:
        directory = self.capture(pilot=True)
        report = build_report(directory / "trials.json", self.manifest, self.root)
        self.assertFalse(report["complete"])
        self.assertEqual(report["status"], "incomplete pilot; no acceptance conclusion")
        for row in report["rows"].values():
            self.assertIsNone(row["measured"])
            self.assertEqual(row["warmup_descriptive"]["blocks"], 1)

    def test_rejects_tampered_process_missing_evidence_and_incomplete_mode_coverage(self) -> None:
        cases = []
        process_tamper = self.capture(pilot=True)
        process_path = next(process_tamper.rglob("*.process.json"))
        process = json.loads(process_path.read_text())
        process["wall_ms"] += 1
        process_path.write_text(json.dumps(process), encoding="utf-8")
        self.refresh_inventory(process_tamper)
        cases.append((process_tamper, "does not match raw process evidence"))

        missing = self.capture(pilot=True)
        next(missing.rglob("*.process.json")).unlink()
        cases.append((missing, "raw evidence file inventory or hash changed"))

        coverage = self.capture(pilot=True)
        trials_path = coverage / "trials.json"
        trials = json.loads(trials_path.read_text())
        trials["rows"].pop(next(iter(trials["rows"])))
        trials_path.write_text(json.dumps(trials), encoding="utf-8")
        self.refresh_inventory(coverage)
        cases.append((coverage, "missing or unexpected"))

        wrong_mode = self.capture(pilot=True)
        process_path = next(wrong_mode.rglob("gpu_eval.process.json"))
        process = json.loads(process_path.read_text())
        engine = process["command"].index("--engine") + 1
        process["command"][engine] = "cpu"
        process_path.write_text(json.dumps(process), encoding="utf-8")
        self.refresh_inventory(wrong_mode)
        cases.append((wrong_mode, "mismatched binary command"))

        wrong_rss = self.capture(pilot=True)
        process_path = next(wrong_rss.rglob("*.process.json"))
        process = json.loads(process_path.read_text())
        process["process_max_rss_kib"] += 1
        process_path.write_text(json.dumps(process), encoding="utf-8")
        self.refresh_inventory(wrong_rss)
        cases.append((wrong_rss, "host RSS differs from raw time output"))

        impossible_counters = self.capture(pilot=True)
        process_path = next(impossible_counters.rglob("cpu.process.json"))
        process = json.loads(process_path.read_text())
        process["output_json"]["generations"][0]["reproduction"]["fallback_children"] = 3
        process["output_json_text"] = json.dumps(process["output_json"])
        process_path.write_text(json.dumps(process), encoding="utf-8")
        process_path.with_name("cpu.output.json").write_text(process["output_json_text"])
        trials_path = impossible_counters / "trials.json"
        trials = json.loads(trials_path.read_text())
        row = trials["rows"]["scalar-p3-g2-b256/cpu"]["blocks"][0]
        row["variation_counters"]["generation_repro_fallback_children"][0] = 3
        trials_path.write_text(json.dumps(trials), encoding="utf-8")
        self.refresh_inventory(impossible_counters)
        cases.append((impossible_counters, "fallback children exceed unchanged"))

        for directory, message in cases:
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                build_report(directory / "trials.json", self.manifest, self.root)

    def test_rejects_nonfinite_schema_and_identity_or_binary_mismatch(self) -> None:
        directory = self.capture(pilot=True)
        process_path = next(directory.rglob("*.process.json"))
        process = json.loads(process_path.read_text())
        process["output_json"]["evolve_call_ms"] = float("nan")
        process["output_json_text"] = json.dumps(process["output_json"])
        process_path.write_text(json.dumps(process), encoding="utf-8")
        mode = process_path.name.removesuffix(".process.json")
        process_path.with_name(f"{mode}.output.json").write_text(process["output_json_text"])
        trials = json.loads((directory / "trials.json").read_text())
        key = next(name for name in trials["rows"] if name.endswith(f"/{mode}"))
        trials["rows"][key]["blocks"][0]["numeric_scopes"]["evolve_call_ms"] = float("nan")
        (directory / "trials.json").write_text(json.dumps(trials))
        self.refresh_inventory(directory)
        with self.assertRaisesRegex(ValueError, "finite"):
            build_report(directory / "trials.json", self.manifest, self.root)

        binary_directory = self.capture(pilot=True)
        self.binary.write_text("changed", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "candidate binary"):
            build_report(binary_directory / "trials.json", self.manifest, self.root)

    def test_cli_refuses_output_inside_trial_directory(self) -> None:
        directory = self.capture(pilot=True)
        manifest_path = self.root / "workloads.json"
        manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")
        argv = ["compiled-migration", str(directory / "trials.json"),
                "--workloads", str(manifest_path), "--root", str(self.root),
                "--output", str(directory / "summary.json")]
        with patch.object(sys, "argv", argv), self.assertRaises(SystemExit):
            main()
        self.assertFalse((directory / "summary.json").exists())


if __name__ == "__main__":
    unittest.main()
