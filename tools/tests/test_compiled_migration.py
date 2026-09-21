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

from gagp_tools.experiments.compiled_migration import (  # noqa: E402
    MODES, VARIATION_COUNTERS, extract_measurements, run_manifest, sha256, validate_manifest,
)


def output_payload(generations: int = 1) -> dict:
    return {
        "format_version": "migration-run-v1",
        "load_ms": 0.25,
        "evolve_call_ms": 4.0,
        "init_population_ms": 1.0,
        "gpu_eval_init_ms": 0.0,
        "final_eval_ms": 0.0,
        "total_ms": 4.0,
        "generations": [{
            "eval_ms": 1.0,
            "repro_ms": 2.0,
            "total_ms": 3.0,
            "evaluation": {
                "cpu_compile_ms": 0.2, "gpu_compile_ms": 0.0,
                "gpu_eval_call_ms": 0.0, "gpu_eval_pack_ms": 0.0,
                "gpu_eval_launch_prep_ms": 0.0, "gpu_eval_upload_ms": 0.0,
                "gpu_eval_kernel_ms": 0.0, "gpu_eval_copyback_ms": 0.0,
                "gpu_eval_teardown_ms": 0.0,
            },
            "reproduction": {
                "selection_ms": 0.1, "crossover_ms": 0.1, "mutation_ms": 0.1,
                "prepare_inputs_ms": 0.1, "setup_ms": 0.1, "preprocess_ms": 0.1,
                "pack_ms": 0.1, "upload_ms": 0.1, "kernel_ms": 0.5,
                "copyback_ms": 0.1, "decode_ms": 0.1, "teardown_ms": 0.1,
                "selection_kernel_ms": 0.1, "variation_kernel_ms": 0.4,
                **{name.removeprefix("generation_repro_"): 0 for name in VARIATION_COUNTERS},
            },
            "best_fitness": 0.0,
            "mean_fitness": -1.0,
            "best_nodes": 3,
        } for _ in range(generations)],
    }


class TestCompiledMigration(unittest.TestCase):
    def fixture(self, root: Path) -> tuple[dict, Path]:
        artifacts = {}
        for name in ("generator", "grammar", "cases", "snapshot"):
            path = root / name
            path.write_text(name)
            artifacts[name] = {"path": name, "sha256": sha256(path)}
        binary = root / "candidate"
        binary.write_text("candidate binary")
        manifest = {
            "version": "compiled-migration-workloads-v1",
            "status": "candidate preregistration; measurements pending",
            "warmup_blocks": 3,
            "measured_blocks": 15,
            "analysis_seed": 42,
            "modes": list(MODES),
            "generator": artifacts["generator"],
            "profiles": [],
            "generation_runs": [],
            "workloads": [{
                "id": "scalar-p64-g1-b256", "profile": "scalar", "domain": "int",
                "grammar": artifacts["grammar"], "cases": artifacts["cases"],
                "snapshot": artifacts["snapshot"],
                "args": ["--population-size", "64", "--generations", "1",
                         "--blocksize", "256", "--seed", "42", "--fuel", "20000",
                         "--penalty", "1", "--skip-final-eval", "on",
                         "--retain-final-population", "off"],
            }],
        }
        return manifest, binary

    def test_rejects_each_frozen_artifact_hash_mismatch_before_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, _ = self.fixture(root)
            for kind in ("generator", "grammar", "cases", "snapshot"):
                changed = copy.deepcopy(manifest)
                target = changed["generator"] if kind == "generator" else changed["workloads"][0][kind]
                target["sha256"] = "0" * 64
                with self.subTest(kind=kind), self.assertRaisesRegex(ValueError, "changed or missing"):
                    validate_manifest(changed, root)

    def test_rejects_missing_or_malformed_native_timing_output(self):
        cases = []
        missing_scope = output_payload()
        del missing_scope["evolve_call_ms"]
        cases.append((missing_scope, "missing direct timing scopes"))

        missing_evaluation = output_payload(2)
        del missing_evaluation["generations"][1]["evaluation"]["gpu_eval_upload_ms"]
        cases.append((missing_evaluation, "missing evaluation timings"))

        missing_reproduction = output_payload(2)
        del missing_reproduction["generations"][1]["reproduction"]["decode_ms"]
        cases.append((missing_reproduction, "missing reproduction timings"))

        malformed_generation = output_payload()
        malformed_generation["generations"][0] = []
        cases.append((malformed_generation, "generation 0 must be an object"))

        for payload, message in cases:
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                extract_measurements(payload, len(payload["generations"]), 5.0)

    def test_pilot_is_explicitly_incomplete_and_keeps_warmup_labels(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, binary = self.fixture(root)
            calls = []

            def execute(command, **kwargs):
                calls.append(command)
                out_index = command.index("--out-json") + 1
                Path(command[out_index]).write_text(json.dumps(output_payload()))
                if "-o" in command:
                    Path(command[command.index("-o") + 1]).write_text(
                        "Maximum resident set size (kbytes): 4321\n")
                return SimpleNamespace(returncode=0, stdout="native stdout", stderr="native stderr")

            output = root / "pilot"
            with patch("gagp_tools.experiments.compiled_migration.gpu_state", return_value={}), \
                    patch("gagp_tools.experiments.compiled_migration.subprocess.run", side_effect=execute):
                report = run_manifest(manifest, root, binary, output, 0, block_limit=1)

            self.assertFalse(report["complete"])
            self.assertEqual(report["status"], "incomplete pilot; comparison pending")
            self.assertEqual(len(calls), 5)
            self.assertTrue(all("--action" in command and command[command.index("--action") + 1] == "run"
                                for command in calls))
            self.assertTrue(all("--snapshot" in command and "--grammar-definition" in command
                                for command in calls))
            self.assertTrue(all("--population-json" not in command and "--grammar-config" not in command
                                for command in calls))
            self.assertEqual({len(row["blocks"]) for row in report["rows"].values()}, {1})
            self.assertTrue(all(row["blocks"][0]["warmup"] for row in report["rows"].values()))
            self.assertTrue(all("rtk" in command and "proxy" in command for command in calls))
            process = json.loads(next(output.rglob("*.process.json")).read_text())
            self.assertEqual(process["returncode"], 0)
            self.assertEqual(process["stdout"], "native stdout")
            self.assertIn("output_json", process)
            self.assertEqual(process.get("process_max_rss_kib"), 4321)
            self.assertFalse(any("gpu" in key and "rss" in key for key in process))
            self.assertEqual(set(next(iter(report["rows"].values()))["blocks"][0]["variation_counters"]),
                             set(VARIATION_COUNTERS))
            with self.assertRaises(FileExistsError):
                run_manifest(manifest, root, binary, output, 0, block_limit=1)

    def test_failure_preserves_process_evidence_and_commits_no_partial_mode_block(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest, binary = self.fixture(root)
            calls = 0

            def execute(command, **kwargs):
                nonlocal calls
                calls += 1
                if calls == 2:
                    return SimpleNamespace(returncode=17, stdout="partial", stderr="device failure")
                Path(command[command.index("--out-json") + 1]).write_text(json.dumps(output_payload()))
                return SimpleNamespace(returncode=0, stdout="ok", stderr="")

            output = root / "failed"
            with patch("gagp_tools.experiments.compiled_migration.gpu_state", return_value={}), \
                    patch("gagp_tools.experiments.compiled_migration.subprocess.run", side_effect=execute):
                with self.assertRaisesRegex(RuntimeError, "evidence retained"):
                    run_manifest(manifest, root, binary, output, 0, block_limit=1)

            processes = sorted(output.rglob("*.process.json"))
            self.assertEqual(len(processes), 2)
            failed = [json.loads(path.read_text()) for path in processes if json.loads(path.read_text())["returncode"]]
            self.assertEqual(failed[0]["returncode"], 17)
            self.assertEqual(failed[0]["stderr"], "device failure")
            report = json.loads((output / "trials.json").read_text())
            self.assertEqual(report["status"], "failed; comparison pending")
            self.assertTrue(all(not row["blocks"] for row in report["rows"].values()))


if __name__ == "__main__":
    unittest.main()
