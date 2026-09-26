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
    MODES, canonical_hash, extract_scopes, measurement_command, run_manifest, sha256,
    uses_cpu_donor_replay, validate_manifest, workload_for_role, workload_hash,
    workload_modes, workload_scopes, validate_grammar_dependencies,
)
from gagp_tools.reports.audit_migration_trials import audit_trials, check_fitness_agreement
from gagp_tools.experiments.freeze_migration_workloads import public_value


def blocks(count=15, cpu_after=100, mode_after=10):
    return [{"block_id": i, "warmup": i < 3, "cpu_before_ms": 100,
             "mode_before_ms": 10, "cpu_after_ms": cpu_after,
             "mode_after_ms": mode_after} for i in range(count + 3)]


def mapped_comparison(mapping_kind="typed-search-space", program_identity="mapped"):
    return {"gate_eligible": True, "mapping_kind": mapping_kind,
            "case_identity": "exact", "limits_identity": "exact",
            "program_identity": program_identity, "limitations": [],
            "evidence": ["fixture-sha256"]}


class TestGrammarMigration(unittest.TestCase):
    def test_grammar_dependency_closure_nested_shared_and_invalid(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "parts").mkdir()
            entry = root / "grammar.json"
            left, right = root / "parts/left.json", root / "right.json"
            shared = root / "shared.json"
            entry.write_text('{"imports":["parts/left.json","right.json"]}')
            left.write_text('{"imports":["../shared.json"]}')
            right.write_text('{"imports":["shared.json"]}')
            shared.write_text("{}")

            def record(path):
                return {"path": str(path.relative_to(root)), "sha256": sha256(path)}

            artifact = record(entry)
            artifact["dependencies"] = [record(p) for p in (left, right, shared)]
            validate_grammar_dependencies(artifact, root)
            old_hash = canonical_hash(artifact)
            shared.write_text('{"value":2}')
            with self.assertRaisesRegex(ValueError, "grammar dependency"):
                validate_grammar_dependencies(artifact, root)
            artifact["dependencies"][-1] = record(shared)
            self.assertNotEqual(old_hash, canonical_hash(artifact))
            validate_grammar_dependencies(artifact, root)
            artifact["dependencies"].append(record(shared))
            with self.assertRaisesRegex(ValueError, "duplicate"):
                validate_grammar_dependencies(artifact, root)
            artifact["dependencies"].pop()
            shared.write_text('{"imports":["grammar.json"]}')
            artifact["dependencies"][-1] = record(shared)
            with self.assertRaisesRegex(ValueError, "cyclic"):
                validate_grammar_dependencies(artifact, root)
            shared.unlink()
            with self.assertRaisesRegex(ValueError, "missing"):
                validate_grammar_dependencies(artifact, root)
            artifact["dependencies"] = []
            with self.assertRaisesRegex(ValueError, "missing grammar import"):
                validate_grammar_dependencies(artifact, root)
            entry.write_text('{"imports":["https://example.com/package.json"]}')
            with self.assertRaisesRegex(ValueError, "relative paths"):
                validate_grammar_dependencies(artifact, root)

    def test_fitness_preserves_reference_backend_rounding(self):
        vectors = {(role, mode): [-835.5134641669065 if mode == "cpu" else -835.5134641669028]
                   for role in ("before", "after") for mode in ("cpu", "gpu_eval")}
        self.assertGreater(check_fitness_agreement(vectors, ("before", "after"), ("cpu", "gpu_eval")), 0)
        vectors["after", "gpu_eval"][0] += 1e-12
        with self.assertRaisesRegex(ValueError, "same engine"):
            check_fitness_agreement(vectors, ("before", "after"), ("cpu", "gpu_eval"))
        for role in ("before", "after"):
            vectors[role, "gpu_eval"] = [-835.513]
        # A preserved bounded GPU fallback can differ from an exact CPU result;
        # the migration gate must report it without redefining baseline behavior.
        self.assertGreater(check_fitness_agreement(vectors, ("before", "after"), ("cpu", "gpu_eval")), 1e-9)

    def test_diagnostic_observations_are_not_timing_evidence(self):
        with self.assertRaisesRegex(ValueError, "diagnostic observations"):
            extract_scopes({"format_version": "migration-run-v1", "diagnostic_only": True,
                            "gpu_eval_init_ms": 3, "evolve_call_ms": 10,
                            "generations": [{"total_ms": 7}]}, 20)

    def test_v2_routes_distinct_artifacts_args_and_hashes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            records = {}
            for name in ("before.snapshot", "before.cases", "after.snapshot", "after.cases", "after.grammar"):
                path = root / name
                path.write_text("{}" if name.endswith(".grammar") else name)
                records[name] = {"path": name, "sha256": sha256(path)}
            base_args = ["--population-size", "64", "--generations", "1", "--blocksize", "256",
                         "--seed", "42", "--fuel", "100"]
            workload = {"pair_id": "scalar-p64", "measurement": "evolution",
                "comparison": mapped_comparison(),
                "before": {"snapshot": records["before.snapshot"], "cases": records["before.cases"],
                           "args": base_args},
                "after": {"snapshot": records["after.snapshot"], "cases": records["after.cases"],
                          "grammar": records["after.grammar"], "args": base_args[:-1] + ["200"]}}
            manifest = {"version": "migration-workloads-v2", "warmup_blocks": 3,
                "measured_blocks": 15, "analysis_seed": 42, "modes": list(MODES),
                "workloads": [workload]}
            validate_manifest(manifest, root)
            before = measurement_command(workload, "cpu", root / "old", root, root / "before.json",
                                         role="before")
            after = measurement_command(workload, "cpu", root / "new", root, root / "after.json",
                                        role="after")
            self.assertIn(str((root / "before.snapshot").resolve()), before)
            self.assertNotIn("--grammar-definition", before)
            self.assertIn(str((root / "after.snapshot").resolve()), after)
            self.assertIn("--grammar-definition", after)
            self.assertEqual(after[after.index("--fuel") + 1], "200")
            self.assertNotEqual(workload_hash(workload, "before"), workload_hash(workload, "after"))
            self.assertEqual(workload_hash(workload, "before"),
                             canonical_hash(workload_for_role(workload, "before")))

            mode_manifest = copy.deepcopy(manifest)
            mode_workload = mode_manifest["workloads"][0]
            gpu_grammar = root / "gpu.grammar"
            gpu_grammar.write_text('{"policy":"GPU donor policy"}')
            override = {"path": gpu_grammar.name, "sha256": sha256(gpu_grammar)}
            mode_workload["after"]["grammar_by_mode"] = {
                mode: override for mode in ("gpu_repro", "gpu_repro_overlap")}
            validate_manifest(mode_manifest, root)
            imported = root / "shared.json"
            imported.write_text("{}")
            gpu_grammar.write_text('{"imports":["shared.json"]}')
            override["sha256"] = sha256(gpu_grammar)
            with self.assertRaisesRegex(ValueError, "import closure"):
                validate_manifest(mode_manifest, root)
            override["dependencies"] = [{"path": imported.name, "sha256": sha256(imported)}]
            validate_manifest(mode_manifest, root)
            imported.write_text('{"changed":true}')
            with self.assertRaisesRegex(ValueError, "grammar dependency"):
                validate_manifest(mode_manifest, root)
            imported.write_text("{}")
            self.assertNotEqual(workload_hash(workload, "after"), workload_hash(mode_workload, "after"))
            self.assertEqual(workload_hash(workload, "before"), workload_hash(mode_workload, "before"))
            for mode in ("cpu", "gpu_eval", "gpu_repro", "gpu_repro_overlap"):
                command = measurement_command(mode_workload, mode, root / "new", root,
                                              root / "mode.json", role="after")
                expected = gpu_grammar if mode.startswith("gpu_repro") else root / "after.grammar"
                self.assertEqual(command[command.index("--grammar-definition") + 1], str(expected))
            for invalid in ({}, {"unknown": override}, {"cpu": None}):
                broken = copy.deepcopy(mode_manifest)
                broken["workloads"][0]["after"]["grammar_by_mode"] = invalid
                with self.assertRaisesRegex(ValueError, "grammar_by_mode"):
                    validate_manifest(broken, root)
            reduced = copy.deepcopy(mode_manifest)
            reduced.update(acceptance_protocol="representative-speedup-2026-09-23",
                           warmup_blocks=1, measured_blocks=3)
            first = reduced["workloads"][0]
            first["logical_id"] = "scalar"
            second = copy.deepcopy(first)
            second.update(pair_id="scalar-steady", measurement="steady_eval",
                          session_warmups=1, session_trials=1)
            reduced["workloads"].append(second)
            validate_manifest(reduced, root)
            alternate = root / "alternate"
            alternate.mkdir()
            alternate_grammar = alternate / gpu_grammar.name
            alternate_grammar.write_bytes(gpu_grammar.read_bytes())
            alternate_import = alternate / imported.name
            alternate_import.write_text('{"different_domain":true}')
            saved_overrides = copy.deepcopy(second["after"]["grammar_by_mode"])
            for artifact in second["after"]["grammar_by_mode"].values():
                artifact["path"] = str(alternate_grammar.relative_to(root))
                artifact["dependencies"] = [{"path": str(alternate_import.relative_to(root)),
                                             "sha256": sha256(alternate_import)}]
            # Root bytes match, but distinct imported content must not be pooled
            # into one logical workload across measurement scopes.
            with self.assertRaisesRegex(ValueError, "logical workload scopes"):
                validate_manifest(reduced, root)
            second["after"]["grammar_by_mode"] = saved_overrides
            del second["after"]["grammar_by_mode"]
            with self.assertRaisesRegex(ValueError, "logical workload scopes"):
                validate_manifest(reduced, root)
            gpu_grammar.write_text("changed")
            with self.assertRaisesRegex(ValueError, "grammar_by_mode gpu_repro"):
                validate_manifest(mode_manifest, root)

            budgeted = copy.deepcopy(manifest)
            budgeted_workload = budgeted["workloads"][0]
            budgeted_workload["after"]["args"] += [
                "--source-max-total-nodes", "80", "--source-max-expr-depth", "7"]
            validate_manifest(budgeted, root)
            command = measurement_command(budgeted_workload, "cpu", root / "new", root,
                                          root / "budgeted.json", role="after")
            self.assertEqual(command[command.index("--source-max-total-nodes") + 1], "80")
            self.assertNotEqual(workload_hash(workload, "after"),
                                workload_hash(budgeted_workload, "after"))
            budgeted_workload["after"]["args"][-1] = "0"
            with self.assertRaisesRegex(ValueError, "must be positive"):
                validate_manifest(budgeted, root)
            budgeted_workload["after"]["args"] = budgeted_workload["after"]["args"][:-2]
            with self.assertRaisesRegex(ValueError, "both limits"):
                validate_manifest(budgeted, root)

            changed = copy.deepcopy(manifest)
            changed["workloads"][0]["after"]["args"] += ["--minimum-dc-frames", "4"]
            validate_manifest(changed, root)
            command = measurement_command(changed["workloads"][0], "cpu", root / "new", root,
                                          root / "reserved.json", role="after")
            self.assertEqual(command[command.index("--minimum-dc-frames") + 1], "4")
            changed["workloads"][0]["before"]["args"] += ["--minimum-dc-frames", "4"]
            with self.assertRaisesRegex(ValueError, "v2 candidate grammar"):
                validate_manifest(changed, root)
            changed = copy.deepcopy(manifest)
            changed["workloads"][0]["after"]["args"] += ["--normalize-typed-storage", "on"]
            validate_manifest(changed, root)
            command = measurement_command(changed["workloads"][0], "cpu", root / "new", root,
                                          root / "normalized.json", role="after")
            self.assertEqual(command[command.index("--normalize-typed-storage") + 1], "on")
            self.assertNotEqual(workload_hash(workload, "after"),
                                workload_hash(changed["workloads"][0], "after"))
            changed["workloads"][0]["before"]["args"] += ["--normalize-typed-storage", "on"]
            with self.assertRaisesRegex(ValueError, "v2 candidate"):
                validate_manifest(changed, root)

            changed = copy.deepcopy(manifest)
            changed["workloads"][0]["after"]["args"] += ["--population-roots", "Main.Int,Main.Float"]
            validate_manifest(changed, root)
            command = measurement_command(changed["workloads"][0], "gpu_repro", root / "new", root,
                                          root / "mixed.json", role="after")
            self.assertEqual(command[command.index("--population-roots") + 1], "Main.Int,Main.Float")
            self.assertNotEqual(workload_hash(workload, "after"), workload_hash(changed["workloads"][0], "after"))
            for invalid in ("", "Main.Int,", "Main.Int,Main.Int", " Main.Int", ",".join(f"R{i}" for i in range(9))):
                broken = copy.deepcopy(changed)
                broken["workloads"][0]["after"]["args"][-1] = invalid
                with self.assertRaisesRegex(ValueError, "population roots require"):
                    validate_manifest(broken, root)
            broken = copy.deepcopy(changed)
            broken["workloads"][0]["before"]["args"] += ["--population-roots", "Main.Int"]
            with self.assertRaisesRegex(ValueError, "population roots require"):
                validate_manifest(broken, root)
            broken = copy.deepcopy(changed)
            del broken["workloads"][0]["after"]["grammar"]
            with self.assertRaisesRegex(ValueError, "population roots require"):
                validate_manifest(broken, root)

            changed = copy.deepcopy(manifest)
            changed["workloads"][0]["after"]["cases"]["sha256"] = "0" * 64
            with self.assertRaisesRegex(ValueError, "after cases"):
                validate_manifest(changed, root)
            (root / "after.cases").unlink()
            with self.assertRaisesRegex(ValueError, "after cases"):
                validate_manifest(manifest, root)
            changed = copy.deepcopy(manifest)
            del changed["workloads"][0]["comparison"]
            with self.assertRaisesRegex(ValueError, "comparison metadata"):
                validate_manifest(changed, root)

    def test_v1_role_helpers_preserve_workload_and_hash(self):
        workload = {"id": "legacy", "args": ["--fuel", "100"],
                    "snapshot": {"path": "old", "sha256": "hash"}}
        self.assertIs(workload_for_role(workload, "before"), workload)
        self.assertIs(workload_for_role(workload, "after"), workload)
        self.assertEqual(workload_hash(workload, "before"), canonical_hash(workload))
        self.assertEqual(workload_hash(workload, "after"), canonical_hash(workload))

    def test_v2_runner_writes_role_hashes_and_audit_checks_commands(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            artifacts = {}
            for role in ("before", "after"):
                for kind in ("snapshot", "cases"):
                    path = root / f"{role}.{kind}"
                    path.write_text(f"{role}.{kind}")
                    artifacts[role, kind] = {"path": path.name, "sha256": sha256(path)}
            for binary_name in ("old", "new"):
                (root / binary_name).write_text(binary_name)
            args = ["--population-size", "64", "--generations", "1", "--blocksize", "256",
                    "--seed", "42", "--fuel", "100"]
            workload = {"pair_id": "paired", "measurement": "evolution",
                "comparison": mapped_comparison(),
                "before": {"snapshot": artifacts["before", "snapshot"],
                           "cases": artifacts["before", "cases"], "args": args},
                "after": {"snapshot": artifacts["after", "snapshot"],
                          "cases": artifacts["after", "cases"], "args": args[:-1] + ["200"]}}
            manifest = {"version": "migration-workloads-v2", "warmup_blocks": 3,
                "measured_blocks": 15, "analysis_seed": 42, "modes": list(MODES),
                "workloads": [workload]}
            commands = []

            def execute(command, **kwargs):
                commands.append(command)
                output = Path(command[command.index("--out-json") + 1])
                output.write_text(json.dumps({"format_version": "migration-run-v1",
                    "gpu_eval_init_ms": 1, "evolve_call_ms": 3,
                    "generations": [{"total_ms": 2}]}))
                return SimpleNamespace(returncode=0, stdout="", stderr="")

            directory = root / "trials"
            with patch("gagp_tools.experiments.grammar_migration.gpu_state", return_value={}), \
                    patch("gagp_tools.experiments.grammar_migration.subprocess.run", side_effect=execute):
                report = run_manifest(manifest, root, root / "old", root / "new", directory, 0,
                                      block_limit=1)
            row = report["rows"]["paired/cpu/cli_wall"]
            self.assertEqual(row["workload_before_sha256"], workload_hash(workload, "before"))
            self.assertEqual(row["workload_after_sha256"], workload_hash(workload, "after"))
            self.assertNotEqual(row["workload_before_sha256"], row["workload_after_sha256"])
            self.assertTrue(any(str((root / "before.snapshot").resolve()) in command for command in commands))
            self.assertTrue(any(str((root / "after.snapshot").resolve()) in command for command in commands))
            self.assertEqual(audit_trials(directory, manifest, root)["status"], "pending")

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

    def test_v2_reproduction_routes_legacy_cpu_replay_and_current_cpu_adapter(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            paths = {}
            for name in ("before.snapshot", "before.cases", "before.tape",
                         "after.snapshot", "after.cases", "after.grammar"):
                path = root / name
                path.write_text('{"calls":[]}' if name == "before.tape" else "{}")
                paths[name] = {"path": name, "sha256": sha256(path)}
            args = ["--population-size", "64", "--generations", "1",
                    "--blocksize", "256", "--seed", "42", "--fuel", "20000"]
            workload = {"pair_id": "mapped-repro", "measurement": "steady_repro",
                "session_warmups": 3, "session_trials": 15,
                "comparison": mapped_comparison(),
                "before": {"snapshot": paths["before.snapshot"],
                           "cases": paths["before.cases"], "donor_tape": paths["before.tape"],
                           "cpu_repro_source": "donor_replay", "args": args},
                "after": {"snapshot": paths["after.snapshot"],
                          "cases": paths["after.cases"], "grammar": paths["after.grammar"],
                          "cpu_repro_source": "adapter", "args": args}}
            manifest = {"version": "migration-workloads-v2", "warmup_blocks": 3,
                "measured_blocks": 15, "analysis_seed": 42, "modes": list(MODES),
                "workloads": [workload]}
            validate_manifest(manifest, root)
            self.assertTrue(uses_cpu_donor_replay(workload, "before"))
            self.assertFalse(uses_cpu_donor_replay(workload, "after"))
            before = measurement_command(workload, "cpu", root / "old-adapter", root,
                                         root / "before.json", root / "old-cpu", "before")
            after = measurement_command(workload, "cpu", root / "new-adapter", root,
                                        root / "after.json", role="after")
            self.assertEqual(before[0], str(root / "old-cpu"))
            self.assertIn("--donor-tape", before)
            self.assertEqual(after[:3], [str(root / "new-adapter"), "--action", "repro-steady"])
            self.assertNotIn("--donor-tape", after)

            changed = copy.deepcopy(manifest)
            changed["workloads"][0]["after"]["cpu_repro_source"] = "donor_replay"
            with self.assertRaisesRegex(ValueError, "after donor_tape"):
                validate_manifest(changed, root)
            changed = copy.deepcopy(manifest)
            del changed["workloads"][0]["after"]["grammar"]
            with self.assertRaisesRegex(ValueError, "after adapter reproduction requires a grammar"):
                validate_manifest(changed, root)

            for executable in ("old-adapter", "new-adapter", "old-cpu"):
                (root / executable).write_text(executable)
            calls = []

            def execute(command, **kwargs):
                calls.append(command)
                output = Path(command[command.index("--out-json") + 1])
                gpu = "--action" in command and command[command.index("--repro-backend") + 1] == "gpu"
                payload = {"format_version": ("migration-steady-reproduction-v1" if gpu
                                                else "migration-steady-cpu-reproduction-v1"),
                           "warmups": 3, "measured_trials": 15,
                           "samples": [{"index": i, "warmup": i < 3, "call_ms": 2}
                                       for i in range(18)]}
                if "--donor-tape" in command:
                    payload.update({"generated_calls": 0, "replayed_calls": 0})
                output.write_text(json.dumps(payload))
                return SimpleNamespace(returncode=0, stdout="", stderr="")

            directory = root / "repro-trials"
            with patch("gagp_tools.experiments.grammar_migration.gpu_state", return_value={}), \
                    patch("gagp_tools.experiments.grammar_migration.subprocess.run", side_effect=execute):
                run_manifest(manifest, root, root / "old-adapter", root / "new-adapter",
                             directory, 0, block_limit=1,
                             before_cpu_repro=root / "old-cpu")
            self.assertEqual(len(calls), 4)
            self.assertEqual(sum("--donor-tape" in command for command in calls), 1)
            self.assertEqual(audit_trials(directory, manifest, root)["status"], "pending")

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
                    "engine": command[command.index("--engine") + 1], "fitness": list(range(64)),
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
            self.assertEqual(result["checked_fitness_vectors"], 36)
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

            paired_directory = root / "paired"
            with patch("gagp_tools.experiments.grammar_migration.gpu_state", return_value={}), \
                    patch("gagp_tools.experiments.grammar_migration.subprocess.run", side_effect=execute):
                run_manifest(manifest, root, binary, binary, paired_directory, 0)
            paired_audit = audit_trials(paired_directory, manifest, root)
            self.assertEqual(paired_audit["checked_processes"], 72)
            self.assertEqual(paired_audit["checked_fitness_vectors"], 72)
            raw = paired_directory / "block-003/workload-000/after-gpu_eval.json"
            original = json.loads(raw.read_text())
            for updates, message in (
                    ({"fitness": list(reversed(range(64)))}, "fitness differs"),
                    ({"fitness": [2.0] + [1.0] * 63}, "fitness differs"),
                    ({"fitness": [1.0] * 63}, "frozen population"),
                    ({"fitness": [True] + [1.0] * 63}, "frozen population"),
                    ({"fitness": [float("nan")] + [1.0] * 63}, "frozen population"),
                    ({"engine": "cpu"}, "wrong measurement path")):
                with self.subTest(updates=updates):
                    raw.write_text(json.dumps({**original, **updates}))
                    # Even a consistent file inventory cannot certify wrong results.
                    hashes = paired_directory / "raw-sha256.json"
                    hashes.write_text(json.dumps({str(p.relative_to(paired_directory)): sha256(p)
                        for p in paired_directory.rglob("*.json") if p != hashes}))
                    with self.assertRaisesRegex(ValueError, message):
                        audit_trials(paired_directory, manifest, root)

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

    def test_representative_tolerance_and_bounded_recheck(self):
        protocol = "representative-2026-09-23"
        def observations(count, cpu=100, gpu=10):
            return [{"block_id": i, "warmup": i == 0, "cpu_before_ms": 100,
                     "cpu_after_ms": cpu, "mode_before_ms": 10, "mode_after_ms": gpu}
                    for i in range(count + 1)]
        result = compare_row(observations(3, 105, 10.5), seed=42, protocol=protocol)
        self.assertEqual(result["status"], "pass")
        self.assertEqual(result["resamples"], 0)
        self.assertNotIn("ci95", result["metrics"]["Q"])
        self.assertEqual(compare_row(observations(2), seed=42, protocol=protocol)["status"], "pending")
        self.assertEqual(compare_row(observations(3, 106, 10.6), seed=42, protocol=protocol)["status"], "rerun")
        self.assertEqual(compare_row(observations(5, 106, 10.6), seed=42, protocol=protocol)["status"], "regression")
        # A faster CPU must not conceal a material reduction in GPU speedup.
        self.assertEqual(compare_row(observations(5, 50, 9), seed=42, protocol=protocol)["status"], "regression")
        self.assertEqual(compare_row(observations(3), seed=42)["status"], "pending")
        with self.assertRaisesRegex(ValueError, "unknown migration"):
            compare_row(observations(3), seed=42, protocol="typo")

    def test_speedup_only_protocol_preserves_historical_absolute_gate(self):
        def observations(cpu, gpu, count=3):
            return [{"block_id": i, "warmup": i == 0, "cpu_before_ms": 100,
                     "cpu_after_ms": cpu, "mode_before_ms": 10, "mode_after_ms": gpu}
                    for i in range(count + 1)]
        protocol = "representative-speedup-2026-09-23"
        result = compare_row(observations(200, 20), seed=42, protocol=protocol)
        self.assertEqual(result["status"], "pass")
        self.assertEqual(result["speedup_before"], result["speedup_after"])
        self.assertEqual(result["metrics"]["Q"]["minimum"], 0.95)
        for key in ("A_mode", "A_cpu"):
            self.assertEqual(result["metrics"][key]["estimate"], 0.5)
            self.assertTrue(result["metrics"][key]["reporting_only"])
            self.assertNotIn("minimum", result["metrics"][key])
        self.assertEqual(compare_row(observations(200, 20), seed=42,
            protocol="representative-2026-09-23")["status"], "rerun")
        self.assertEqual(compare_row(observations(95, 10), seed=42,
            protocol=protocol)["status"], "pass")
        self.assertEqual(compare_row(observations(94, 10), seed=42,
            protocol=protocol)["status"], "rerun")
        self.assertEqual(compare_row(observations(94, 10, 5), seed=42,
            protocol=protocol)["status"], "regression")
        self.assertEqual(compare_row(observations(100, 10, 2), seed=42,
            protocol=protocol)["status"], "pending")

    def test_representative_runner_and_audit(self):
        self.check_representative_runner_and_audit("representative-2026-09-23")

    def test_speedup_runner_and_audit(self):
        self.check_representative_runner_and_audit("representative-speedup-2026-09-23")

    def check_representative_runner_and_audit(self, protocol):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            fixture = root / "fixture.json"
            fixture.write_text("{}")
            binary = root / "adapter"
            binary.write_text("test executable identity")
            artifact = {"path": str(fixture), "sha256": sha256(fixture)}
            manifest = {"version": "migration-workloads-v1",
                "acceptance_protocol": protocol,
                "warmup_blocks": 1, "measured_blocks": 3, "analysis_seed": 42,
                "modes": ["cpu", "gpu_eval", "gpu_repro", "gpu_repro_overlap"],
                "workloads": [{"id": "scalar", "cases": artifact, "snapshot": artifact,
                    "measurement": "steady_eval", "session_warmups": 1, "session_trials": 1,
                    "args": ["--population-size", "64", "--generations", "1", "--blocksize", "1024",
                             "--seed", "42", "--fuel", "20000"]}]}
            commands = []
            def execute(command, **kwargs):
                commands.append(command)
                output = Path(command[command.index("--out-json") + 1])
                output.write_text(json.dumps({"format_version": "migration-steady-eval-v1",
                    "engine": command[command.index("--engine") + 1], "fitness": list(range(64)),
                    "warmups": 1, "measured_trials": 1,
                    "samples": [{"index": i, "warmup": i == 0, "call_ms": 2} for i in range(2)]}))
                return SimpleNamespace(returncode=0, stdout="", stderr="")
            directory = root / "trials"
            with patch("gagp_tools.experiments.grammar_migration.gpu_state", return_value={}), \
                    patch("gagp_tools.experiments.grammar_migration.subprocess.run", side_effect=execute):
                report = run_manifest(manifest, root, binary, binary, directory, 0)
            self.assertEqual(len(commands), 16)  # Four pairs, two roles, two engines.
            self.assertTrue(all(command[command.index("--trials") + 1] == "1" for command in commands))
            self.assertEqual(audit_trials(directory, manifest, root)["status"], "complete")
            self.assertEqual(compare_manifest(report)["status"], "pass")
            grouped = copy.deepcopy(manifest)
            grouped["workloads"] = []
            for index in range(6):
                for measurement in ("steady_eval", "evolution"):
                    row = copy.deepcopy(manifest["workloads"][0])
                    row.update(id=f"scalar-{index}-{measurement}",
                               logical_id=f"scalar-{index}", measurement=measurement)
                    if measurement == "evolution":
                        row["args"][row["args"].index("--generations") + 1] = "5"
                    grouped["workloads"].append(row)
            validate_manifest(grouped, root)  # Twelve scopes, six logical inputs.
            changed = copy.deepcopy(grouped)
            changed["workloads"][-1]["logical_id"] = "seventh"
            with self.assertRaisesRegex(ValueError, "six logical workloads"):
                validate_manifest(changed, root)
            changed = copy.deepcopy(grouped)
            row = changed["workloads"][-1]
            row["args"][row["args"].index("--fuel") + 1] = "100"
            with self.assertRaisesRegex(ValueError, "preserve inputs"):
                validate_manifest(changed, root)
            changed = copy.deepcopy(grouped)
            changed["workloads"][-1]["measurement"] = "steady_eval"
            with self.assertRaisesRegex(ValueError, "duplicate measurement"):
                validate_manifest(changed, root)
            for samples in (15, 30):
                changed = copy.deepcopy(manifest)
                changed["workloads"][0]["session_trials"] = samples
                with self.assertRaises(ValueError):
                    validate_manifest(changed, root)
            report.pop("acceptance_protocol")
            (directory / "trials.json").write_text(json.dumps(report))
            with self.assertRaisesRegex(ValueError, "acceptance protocol"):
                audit_trials(directory, manifest, root)

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
        changed = copy.deepcopy(manifest)
        changed["rows"]["scalar/gpu_eval/cold"]["workload_after_sha256"] = "different"
        self.assertEqual(compare_manifest(changed)["status"], "pass")
        changed = copy.deepcopy(manifest)
        changed["rows"]["scalar/gpu_eval/cold"]["timing_source"] = "phase_sum"
        with self.assertRaises(ValueError):
            compare_manifest(changed)
        manifest["required_rows"].append("scalar/gpu_repro/cold")
        with self.assertRaises(ValueError):
            compare_manifest(manifest)
