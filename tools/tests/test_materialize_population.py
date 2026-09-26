from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "cpp/build/gagp_generate_cli"
sys.path.insert(0, str(ROOT / "tools"))

from gagp_tools.cli import main  # noqa: E402


@unittest.skipUnless(GENERATOR.is_file(), "build gagp_generate_cli for integration tests")
class TestMaterializePopulation(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="gagp population ")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.types = ["Int", "Float", "Bool", "Char", "String", "IntList", "FloatList", "StringList"]
        values = ["1", 1.25, True, "a", "text", ["1"], [1.25], ["text"]]
        self.roots = ["Root" + name for name in self.types]
        self.grammar = self.directory / "grammar.json"
        self.grammar.write_text(json.dumps({
            "format_version": "grammar-definition-v2",
            "entry": {"nonterminal": self.roots[0], "type": self.types[0]},
            "search_limits": {"max_nodes": 5, "max_depth": 4},
            "execution_limits": {"fuel": 100},
            "nonterminals": [
                {"id": root, "type": kind, "scope": [], "alternatives": [
                    {"id": "value", "weight": 1,
                     "expression": {"constant": {"type": kind, "values": [value]}}}
                ]}
                for root, kind, value in zip(self.roots, self.types, values)
            ],
        }))
        self.cases = self.directory / "cases.json"
        self.cases.write_text(json.dumps({
            "format_version": "fitness-cases",
            "cases": [{"inputs": {}, "expected": {"type": "int", "value": 1}}],
        }))
        self.output = self.directory / "population.json"
        self.arguments = [
            "benchmark", "population", "--generator", str(GENERATOR),
            "--grammar-definition", str(self.grammar), "--cases", str(self.cases),
            "--population-size", "16", "--seed", "42", "--out", str(self.output),
        ]

    def test_all_types_preserve_requested_order_and_native_replay(self) -> None:
        roots = ",".join(reversed(self.roots))
        self.assertEqual(main([*self.arguments, "--population-roots", roots]), 0)
        original = self.output.read_bytes()
        artifact = json.loads(original)
        self.assertEqual([member["return_type"] for member in artifact["members"]],
                         list(reversed(self.types)) * 2)
        self.assertEqual([member["seed"] for member in artifact["members"]],
                         [str(seed) for seed in range(42, 58)])
        replayed = self.directory / "replayed.json"
        result = subprocess.run([
            str(GENERATOR), "--cases", str(self.cases), "--replay-json", str(self.output),
            "--population-roots", roots, "--out-json", str(replayed),
        ], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(replayed.read_bytes(), original)

    def test_omitted_roots_keep_single_entry(self) -> None:
        self.assertEqual(main(self.arguments), 0)
        artifact = json.loads(self.output.read_text())
        self.assertEqual([member["return_type"] for member in artifact["members"]],
                         ["Int"] * 16)

    def test_invalid_roots_preserve_existing_output(self) -> None:
        self.output.write_text("preserve me")
        for roots in ("", "RootInt,", "RootInt,RootInt", "Unknown"):
            with self.subTest(roots=roots), self.assertRaises(SystemExit):
                main([*self.arguments, "--population-roots", roots])
            self.assertEqual(self.output.read_text(), "preserve me")


if __name__ == "__main__":
    unittest.main()
