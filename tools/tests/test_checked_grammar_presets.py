from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PRESETS = ("all", "num_list", "scalar", "sequence", "string", "string_list")


class TestCheckedGrammarPresets(unittest.TestCase):
    def test_v1_inputs_are_isolated_and_v2_definitions_preserve_their_content(self) -> None:
        current = ROOT / "configs" / "grammar"
        legacy = current / "migration" / "v1"
        self.assertEqual(
            sorted(path.stem for path in legacy.glob("*.json")),
            list(PRESETS),
        )

        for name in PRESETS:
            with self.subTest(name=name):
                source = json.loads((legacy / f"{name}.json").read_text(encoding="utf-8"))
                definition = json.loads((current / f"{name}.json").read_text(encoding="utf-8"))
                self.assertEqual(source["format_version"], "grammar-config")
                self.assertEqual(definition["format_version"], "grammar-definition-v2")
                self.assertTrue(definition["resolved"])
                self.assertGreater(len(definition["nonterminals"]), 0)
                self.assertEqual(definition["search_limits"]["max_nodes"], source["limits"]["max_total_nodes"])
                self.assertEqual(definition["search_limits"]["max_depth"], source["limits"]["max_expr_depth"])
                actual_types = {
                    row["type"]
                    for row in definition["nonterminals"]
                    if row["id"].startswith("Gct1.Expr.")
                }
                type_names = {
                    "bool": "Bool", "char": "Char", "float": "Float", "float_list": "FloatList",
                    "int": "Int", "int_list": "IntList", "string": "String", "string_list": "StringList",
                }
                expected_types = {type_names[key] for key, enabled in source["values"].items() if enabled}
                self.assertEqual(actual_types, expected_types)
                signatures = {
                    alternative["expression"].get("signature", "")
                    for row in definition["nonterminals"]
                    for alternative in row.get("alternatives", [])
                }
                for builtin, enabled in source["builtins"].items():
                    if not enabled:
                        self.assertFalse(any(item.startswith(f"{builtin}(") for item in signatures), builtin)

    def test_profile_identity_comes_from_embedded_content(self) -> None:
        current = ROOT / "configs" / "grammar"
        profiles = {
            name: json.loads((current / "migration" / "v1" / f"{name}.json").read_text(encoding="utf-8"))
            for name in PRESETS
        }
        self.assertEqual(profiles["all"]["values"], profiles["sequence"]["values"])
        self.assertNotEqual(profiles["scalar"]["values"], profiles["num_list"]["values"])
        self.assertNotEqual(profiles["string"]["values"], profiles["string_list"]["values"])
        for name, profile in profiles.items():
            self.assertEqual(profile["profile"], name)
        self.assertEqual(
            json.loads((current / "all.json").read_text(encoding="utf-8")),
            json.loads((current / "sequence.json").read_text(encoding="utf-8")),
        )


if __name__ == "__main__":
    unittest.main()
