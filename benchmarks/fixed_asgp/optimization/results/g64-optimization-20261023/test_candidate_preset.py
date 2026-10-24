import copy
import json
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

import candidate_preset
import serial


class TestFrozenCandidatePreset(unittest.TestCase):
    def fixture(self, root):
        binary = root / "logs/bench"
        binary.parent.mkdir()
        binary.write_text("fixture, not an executable")
        selection = dict(binary=str(binary), binary_sha256=serial.sha(binary), source_build="fixture7",
                         candidate_extra={"GAGP_GPU_DENSE_CASE_TILES": "tuple"})
        provenance = dict(selected_binary_sha256=selection["binary_sha256"], cpp_source_sha256={"cpp/fixture": "fixture"})
        settings = dict(GAGP_GPU_PAYLOAD_WORKSPACE_MIB="16384", GAGP_HOST_THREADS="1",
                        GAGP_GPU_PHASE_POPULATION="1", GAGP_CUDA_DEVICE="0")
        freeze = dict(format="gagp-g64-speed-freeze-v1", started_epoch=serial.START, deadline_epoch=serial.DEADLINE,
                      tasks=list(serial.PAPER_TASKS), population=1024, generations=64, repetitions=3,
                      settings=settings, variants={"candidate": dict(binary=str(binary), binary_sha256=selection["binary_sha256"],
                              extra=selection["candidate_extra"])}, freeze_epoch=1, data_seed=20261024)
        return freeze, selection, provenance

    def test_exported_recipe_retains_complete_frozen_settings_and_source(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            freeze, selection, provenance = self.fixture(root)
            recipe = candidate_preset.make_preset(freeze, selection, provenance, root)
        self.assertEqual(recipe["binary"], "logs/bench")
        self.assertEqual(recipe["settings"], dict(freeze["settings"], **selection["candidate_extra"]))
        self.assertEqual(recipe["source_sha256"], provenance["cpp_source_sha256"])
        self.assertEqual(recipe["default_generations"], 64)
        self.assertEqual(recipe["physical_caps"], dict(ast_nodes=2048, depth=24, phase_nodes=1024))

    def test_wrong_scope_clock_selection_or_resource_recipe_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            freeze, selection, provenance = self.fixture(root)
            for key, value in [("started_epoch", 0), ("generations", 8), ("population", 16384), ("repetitions", 1)]:
                with self.assertRaises(ValueError):
                    candidate_preset.make_preset(dict(freeze, **{key: value}), selection, provenance, root)
            for key, value in [("GAGP_GPU_PAYLOAD_WORKSPACE_MIB", "8192"), ("GAGP_HOST_THREADS", "2"),
                               ("GAGP_GPU_PHASE_POPULATION", "0"), ("GAGP_SEARCH_EXPORT_BEST_ONLY", "1")]:
                changed = copy.deepcopy(freeze)
                changed["settings"][key] = value
                with self.assertRaises(ValueError):
                    candidate_preset.make_preset(changed, selection, provenance, root)
            with self.assertRaises(ValueError):
                candidate_preset.make_preset(freeze, dict(selection, candidate_extra={}), provenance, root)

    def test_invocation_has_explicit_cases_and_ignores_ambient_gagp_switches(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            recipe = candidate_preset.make_preset(*self.fixture(root), root)
            command, environment, outputs = candidate_preset.invocation(recipe, root, root / "source", root / "prepared",
                root / "grammar", 16384, root / "user/one.json", 19, ambient={"PATH": "kept", "GAGP_CUDA_DEVICE": "9",
                "GAGP_SEARCH_EXPORT_BEST_ONLY": "1", "LD_PRELOAD": "ambient-allocator", "MALLOC_CONF": "ambient"})
            self.assertEqual(command[-3:], ["16384", "search_gpu_overlap", str(root / "user/one.json")])
            self.assertEqual(environment["GAGP_BM_CASE_COUNT"], "19")
            self.assertEqual(environment["GAGP_SEARCH_GENERATIONS"], "64")
            self.assertEqual(environment["GAGP_CUDA_DEVICE"], "0")
            self.assertNotIn("GAGP_SEARCH_EXPORT_BEST_ONLY", environment)
            self.assertNotIn("LD_PRELOAD", environment)
            self.assertEqual(environment["PATH"], "kept")
            self.assertEqual(len(set(outputs)), 5)
            for cases in (None, 0, -1, True):
                with self.assertRaises(ValueError):
                    candidate_preset.invocation(recipe, root, "s", "p", "g", 1024, root / "one.json", cases)

    def test_formal_output_namespace_path_collisions_and_binary_escape_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            recipe = candidate_preset.make_preset(*self.fixture(root), root)
            with mock.patch.object(serial, "RAW", root / "formal"):
                for out in (root / "formal/new.json", root / "user/one.log"):
                    with self.assertRaises(ValueError):
                        candidate_preset.invocation(recipe, root, "s", "p", "g", 1024, out, 200)
            with self.assertRaises(ValueError):
                candidate_preset.invocation(dict(recipe, binary="../other/bench"), root, "s", "p", "g", 1024, root / "one.json", 200)

    def test_failed_spawn_or_exit_is_one_attempt_and_existing_receipt_is_preserved(self):
        for error, code in ((OSError("fixture spawn failure"), 127), (None, 3)):
            with self.subTest(code=code), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                recipe = candidate_preset.make_preset(*self.fixture(root), root)
                command, environment, outputs = candidate_preset.invocation(recipe, root, "s", "p", "g", 1024, root / "one.json", 200)
                with mock.patch.object(candidate_preset.subprocess, "run", side_effect=error,
                                       return_value=SimpleNamespace(returncode=code)) as run:
                    self.assertEqual(candidate_preset.execute_once(command, environment, outputs, {}, recipe), code)
                    receipt_bytes = outputs[-1].read_bytes()
                    self.assertEqual(json.loads(receipt_bytes)["returncode"], code)
                    with self.assertRaises(ValueError):
                        candidate_preset.execute_once(command, environment, outputs, {}, recipe)
                    self.assertEqual(outputs[-1].read_bytes(), receipt_bytes)
                    self.assertEqual(run.call_count, 1)

    def test_relative_export_paths_retain_origin_and_refuse_overwrite(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            freeze, selection, provenance = self.fixture(root)
            provenance_path, selection_path, freeze_path, out = [root / name for name in
                ("provenance.json", "selection.json", "freeze.json", "preset.json")]
            provenance_path.write_text(json.dumps(provenance))
            freeze.update(source_provenance=str(provenance_path), source_provenance_sha256=serial.sha(provenance_path))
            selection_path.write_text(json.dumps(selection))
            freeze_path.write_text(json.dumps(freeze))
            with mock.patch.object(serial, "ROOT", root), mock.patch.object(serial, "GPU_LOCK", root / "fixture.lock"):
                candidate_preset.export(Path(os.path.relpath(freeze_path)), Path(os.path.relpath(selection_path)), Path(os.path.relpath(out)))
                before = out.read_bytes()
                recipe = json.loads(before)
                self.assertEqual(recipe["origin"]["freeze"], "freeze.json")
                self.assertEqual(recipe["origin"]["freeze_sha256"], serial.sha(freeze_path))
                with self.assertRaises(ValueError):
                    candidate_preset.export(freeze_path, selection_path, out)
                self.assertEqual(out.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
