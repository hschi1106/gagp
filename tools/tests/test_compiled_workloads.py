from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from gagp_tools.experiments.freeze_compiled_workloads import (  # noqa: E402
    BLOCKS,
    DOMAINS,
    GENERATIONS,
    MODES,
    SIZES,
    VERSION,
    _memo_expected,
    freeze,
    sha256,
)


class TestCompiledWorkloads(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.repository = self.root / "repository"
        definitions = self.repository / "configs" / "grammar_definitions"
        definitions.mkdir(parents=True)
        source = Path(__file__).resolve().parents[2] / "configs" / "grammar_definitions"
        for name in ("bounded_sequence", "bounded_memo"):
            (definitions / f"{name}.json").write_bytes((source / f"{name}.json").read_bytes())
        self.generator = self.root / "gagp_generate_cli"
        self.generator.write_text("mock generator identity\n")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    @staticmethod
    def generate(command: list[str], **kwargs: object) -> SimpleNamespace:
        if command[:2] != ["rtk", "proxy"]:
            return SimpleNamespace(returncode=2, stdout="", stderr="missing rtk proxy")
        grammar = Path(command[command.index("--grammar-definition") + 1])
        size = int(command[command.index("--population-size") + 1])
        seed = int(command[command.index("--seed") + 1])
        output = Path(command[command.index("--out-json") + 1])
        payload = {
            "format_version": "grammar-population-v2",
            "grammar_hash": sha256(grammar),
            "count": size,
            "seed": seed,
            "members": [],
        }
        output.write_text(json.dumps(payload, sort_keys=True) + "\n")
        return SimpleNamespace(returncode=0, stdout=f"count={size}\n", stderr="")

    def materialize(self) -> tuple[Path, dict]:
        output = self.root / "artifacts"
        with patch(
            "gagp_tools.experiments.freeze_compiled_workloads.subprocess.run",
            side_effect=self.generate,
        ) as invoked:
            manifest = freeze(self.repository, self.generator, output, seed=381)
        self.assertEqual(invoked.call_count, 20)
        return output, manifest

    def test_manifest_preregisters_complete_candidate_grid(self) -> None:
        output, manifest = self.materialize()
        self.assertEqual(manifest["version"], VERSION)
        self.assertEqual(manifest["status"], "candidate preregistration; measurements pending")
        self.assertEqual(manifest["modes"], list(MODES))
        self.assertEqual(manifest["generator"]["sha256"], sha256(self.generator))
        self.assertEqual(len(manifest["profiles"]), 10)
        self.assertEqual(len(manifest["generation_runs"]), 20)
        self.assertEqual(len(manifest["workloads"]), 80)

        expected_grid = {
            (profile, size, generations, block)
            for profile in [row["id"] for row in manifest["profiles"]]
            for size in SIZES
            for generations in GENERATIONS
            for block in BLOCKS
        }
        actual_grid = set()
        profiles = {row["id"]: row for row in manifest["profiles"]}
        for workload in manifest["workloads"]:
            options = dict(zip(workload["args"][::2], workload["args"][1::2]))
            actual_grid.add((workload["profile"], int(options["--population-size"]),
                             int(options["--generations"]), int(options["--blocksize"])))
            grammar = json.loads((output / workload["grammar"]["path"]).read_text())
            self.assertEqual(int(options["--fuel"]), grammar["execution_limits"]["fuel"])
            self.assertEqual(int(options["--fuel"]), profiles[workload["profile"]]["fuel"])
        self.assertEqual(actual_grid, expected_grid)

        for run in manifest["generation_runs"]:
            self.assertEqual(run["command"][:3], ["rtk", "proxy", str(self.generator.resolve())])
            self.assertEqual(run["returncode"], 0)
            self.assertIn("output", run)
            self.assertEqual(run["record"]["sha256"], sha256(output / run["record"]["path"]))

    def test_hashes_domains_and_profile_shapes_are_exact(self) -> None:
        output, manifest = self.materialize()
        typed_domains = {row[0] for row in DOMAINS}
        typed = [row for row in manifest["profiles"] if row["kind"] == "typed_lexical_capture"]
        self.assertEqual({row["domain"] for row in typed}, typed_domains)
        self.assertEqual(len(typed), 8)

        for profile in manifest["profiles"]:
            for kind in ("grammar", "cases"):
                artifact = profile[kind]
                self.assertEqual(artifact["sha256"], sha256(output / artifact["path"]))
            grammar = json.loads((output / profile["grammar"]["path"]).read_text())
            cases = json.loads((output / profile["cases"]["path"]).read_text())
            self.assertEqual(len(cases["cases"]), 1024)
            if profile["kind"] == "typed_lexical_capture":
                expression = grammar["nonterminals"][0]["alternatives"][0]["expression"]
                type_name = grammar["entry"]["type"]
                self.assertEqual(expression["signature"], f"let({type_name},{type_name})->{type_name}")
                self.assertEqual(expression["bind"], {"1": ["x"]})
                self.assertEqual(expression["args"][1], {"bound": "x"})

        memo = next(row for row in manifest["profiles"] if row["id"] == "bounded_memo")
        memo_grammar = json.loads((output / memo["grammar"]["path"]).read_text())
        limits = memo_grammar["nonterminals"][0]["alternatives"][0]["expression"]["structured"]["plan"]["limits"]
        self.assertEqual(memo["memo_cells"], 128)
        self.assertEqual(limits["cells"], 128)
        memo_cases = json.loads((output / memo["cases"]["path"]).read_text())
        self.assertEqual(_memo_expected(7, 7, 8, 8, 3), 84303)
        self.assertEqual(memo_cases["cases"][0]["expected"], {"type": "int", "value": 84303})
        original = self.repository / memo["source_grammar"]["path"]
        self.assertEqual(memo["source_grammar"]["sha256"], sha256(original))
        self.assertEqual(json.loads(original.read_text())["nonterminals"][0]["alternatives"][0]
                         ["expression"]["structured"]["plan"]["limits"]["cells"], 1024)

        disk_manifest = json.loads((output / "workloads.json").read_text())
        self.assertEqual(disk_manifest, manifest)
        for workload in manifest["workloads"]:
            for kind in ("grammar", "cases", "snapshot"):
                artifact = workload[kind]
                self.assertEqual(artifact["sha256"], sha256(output / artifact["path"]))

    def test_refuses_an_existing_output_without_invoking_generator(self) -> None:
        output = self.root / "artifacts"
        output.mkdir()
        marker = output / "keep"
        marker.write_text("unchanged")
        with patch("gagp_tools.experiments.freeze_compiled_workloads.subprocess.run") as invoked:
            with self.assertRaises(FileExistsError):
                freeze(self.repository, self.generator, output)
        invoked.assert_not_called()
        self.assertEqual(marker.read_text(), "unchanged")

    def test_failed_generation_does_not_publish_manifest(self) -> None:
        output = self.root / "artifacts"
        failed = SimpleNamespace(returncode=7, stdout="partial", stderr="invalid grammar")
        with patch("gagp_tools.experiments.freeze_compiled_workloads.subprocess.run", return_value=failed):
            with self.assertRaisesRegex(RuntimeError, "typed_capture_int p64"):
                freeze(self.repository, self.generator, output)
        self.assertFalse((output / "workloads.json").exists())
        record = json.loads((output / "typed_capture_int-p64.generation.json").read_text())
        self.assertEqual(record["returncode"], 7)
        self.assertEqual(record["stdout"], "partial")
        self.assertEqual(record["stderr"], "invalid grammar")


if __name__ == "__main__":
    unittest.main()
