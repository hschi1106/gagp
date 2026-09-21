from __future__ import annotations

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"


class TestGrammarCommands(unittest.TestCase):
    def run_cli(
        self,
        *args: str,
        cwd: Path = ROOT,
        env: dict[str, str] | None = None,
    ) -> subprocess.CompletedProcess[str]:
        process_env = os.environ.copy()
        process_env["PYTHONPATH"] = str(TOOLS)
        if env:
            process_env.update(env)
        return subprocess.run(
            ["python3", "-m", "gagp_tools", *args],
            cwd=cwd,
            env=process_env,
            text=True,
            capture_output=True,
        )

    def make_fake_native(self, root: Path) -> Path:
        native = root / "fake-native.py"
        native.write_text(
            "#!/usr/bin/env python3\n"
            "import json, os, pathlib, sys\n"
            "pathlib.Path(os.environ['GAGP_CAPTURE']).write_text(json.dumps(sys.argv[1:]))\n"
            "print('native stdout')\n"
            "print('native stderr', file=sys.stderr)\n"
            "raise SystemExit(int(os.environ.get('GAGP_EXIT', '0')))\n",
            encoding="utf-8",
        )
        native.chmod(0o755)
        return native

    def test_init_copies_each_named_example_exactly(self) -> None:
        with tempfile.TemporaryDirectory(prefix="gagp_grammar_init_") as td:
            root = Path(td)
            examples = root / "examples"
            examples.mkdir()
            for name in ("scalar", "sequence", "template", "memo"):
                content = f'{{"example":"{name}"}}\n'.encode()
                (examples / f"{name}.json").write_bytes(content)
                output = root / f"output-{name}.json"

                result = self.run_cli(
                    "grammar",
                    "init",
                    "--example",
                    name,
                    "--out",
                    str(output),
                    "--examples-root",
                    str(examples),
                )

                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(output.read_bytes(), content)
                self.assertEqual(list(root.glob(f".{output.name}.tmp.*")), [])

    def test_init_refuses_to_overwrite_an_existing_output(self) -> None:
        with tempfile.TemporaryDirectory(prefix="gagp_grammar_init_existing_") as td:
            root = Path(td)
            examples = root / "examples"
            examples.mkdir()
            (examples / "scalar.json").write_text("new", encoding="utf-8")
            output = root / "grammar.json"
            output.write_text("keep", encoding="utf-8")

            result = self.run_cli(
                "grammar",
                "init",
                "--example",
                "scalar",
                "--out",
                str(output),
                "--examples-root",
                str(examples),
            )

            self.assertEqual(result.returncode, 2)
            self.assertIn("refusing to overwrite", result.stderr)
            self.assertEqual(output.read_text(encoding="utf-8"), "keep")

    def test_init_discovers_checkout_from_a_nested_working_directory(self) -> None:
        with tempfile.TemporaryDirectory(prefix="gagp_grammar_checkout_") as td:
            checkout = Path(td)
            examples = checkout / "configs" / "grammar" / "examples" / "authoring"
            examples.mkdir(parents=True)
            (examples / "memo.json").write_text("memo example\n", encoding="utf-8")
            nested = checkout / "work" / "nested"
            nested.mkdir(parents=True)
            output = checkout / "memo-copy.json"

            result = self.run_cli(
                "grammar", "init", "--example", "memo", "--out", str(output), cwd=nested
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(output.read_text(encoding="utf-8"), "memo example\n")

    def test_init_reports_missing_example_without_creating_output(self) -> None:
        with tempfile.TemporaryDirectory(prefix="gagp_grammar_init_missing_") as td:
            root = Path(td)
            examples = root / "examples"
            examples.mkdir()
            output = root / "grammar.json"

            result = self.run_cli(
                "grammar",
                "init",
                "--example",
                "template",
                "--out",
                str(output),
                "--examples-root",
                str(examples),
            )

            self.assertEqual(result.returncode, 2)
            self.assertIn("grammar example does not exist", result.stderr)
            self.assertFalse(output.exists())

    def test_native_grammar_commands_forward_arguments_and_process_streams(self) -> None:
        with tempfile.TemporaryDirectory(prefix="gagp_grammar_native_") as td:
            root = Path(td)
            native = self.make_fake_native(root)
            grammar = root / "not-validated-by-python.json"
            grammar.write_text("this is deliberately not JSON", encoding="utf-8")
            capture = root / "capture.json"
            env = {"GAGP_CAPTURE": str(capture), "GAGP_EXIT": "17"}
            commands = {
                "validate": ["validate", "--grammar-definition", str(grammar)],
                "inspect": ["inspect", "--grammar-definition", str(grammar)],
                "resolve": [
                    "resolve",
                    "--grammar-definition",
                    str(grammar),
                    "--out-json",
                    str(root / "resolved.json"),
                ],
            }

            for action, expected in commands.items():
                with self.subTest(action=action):
                    arguments = [
                        "grammar",
                        action,
                        "--native",
                        str(native),
                        "--grammar-definition",
                        str(grammar),
                    ]
                    if action == "resolve":
                        arguments.extend(["--out", str(root / "resolved.json")])
                    result = self.run_cli(*arguments, env=env)
                    self.assertEqual(result.returncode, 17)
                    self.assertEqual(result.stdout, "native stdout\n")
                    self.assertEqual(result.stderr, "native stderr\n")
                    self.assertEqual(json.loads(capture.read_text(encoding="utf-8")), expected)

    def test_native_adapters_use_checkout_build_defaults(self) -> None:
        with tempfile.TemporaryDirectory(prefix="gagp_grammar_defaults_") as td:
            checkout = Path(td)
            build = checkout / "cpp" / "build"
            build.mkdir(parents=True)
            grammar_native = self.make_fake_native(build)
            grammar_native.rename(build / "gagp_grammar_cli")
            capture = checkout / "grammar-capture.json"

            result = self.run_cli(
                "grammar",
                "validate",
                "--grammar-definition",
                "grammar.json",
                cwd=checkout,
                env={"GAGP_CAPTURE": str(capture)},
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                json.loads(capture.read_text(encoding="utf-8")),
                ["validate", "--grammar-definition", "grammar.json"],
            )

            result = self.run_cli(
                "grammar",
                "resolve",
                "--grammar-definition",
                "grammar.json",
                cwd=checkout,
                env={"GAGP_CAPTURE": str(capture)},
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                json.loads(capture.read_text(encoding="utf-8")),
                ["resolve", "--grammar-definition", "grammar.json"],
            )

            migrate_native = self.make_fake_native(build)
            migrate_native.rename(build / "gagp_migrate_artifact")
            result = self.run_cli(
                "grammar",
                "migrate",
                "--input",
                "legacy.json",
                "--out",
                "current.json",
                cwd=checkout,
                env={"GAGP_CAPTURE": str(capture)},
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                json.loads(capture.read_text(encoding="utf-8")),
                ["--input", "legacy.json", "--out", "current.json"],
            )

    def test_migrate_forwards_only_current_native_route_flags(self) -> None:
        with tempfile.TemporaryDirectory(prefix="gagp_grammar_migrate_") as td:
            root = Path(td)
            native = self.make_fake_native(root)
            capture = root / "capture.json"
            input_path = root / "legacy.json"
            output = root / "migrated.json"
            cases = root / "cases.json"

            result = self.run_cli(
                "grammar",
                "migrate",
                "--native",
                str(native),
                "--input",
                str(input_path),
                "--out",
                str(output),
                "--cases",
                str(cases),
                "--conversion-profile",
                "constrained-intent-v1",
                "--fuel",
                "1000",
                "--max-nodes",
                "128",
                "--max-depth",
                "32",
                env={"GAGP_CAPTURE": str(capture)},
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                json.loads(capture.read_text(encoding="utf-8")),
                [
                    "--input",
                    str(input_path),
                    "--out",
                    str(output),
                    "--cases",
                    str(cases),
                    "--conversion-profile",
                    "constrained-intent-v1",
                    "--fuel",
                    "1000",
                    "--max-nodes",
                    "128",
                    "--max-depth",
                    "32",
                ],
            )

    def test_migrate_omits_optional_route_flags_when_absent(self) -> None:
        with tempfile.TemporaryDirectory(prefix="gagp_grammar_migrate_minimal_") as td:
            root = Path(td)
            native = self.make_fake_native(root)
            capture = root / "capture.json"
            input_path = root / "legacy.json"
            output = root / "migrated.json"

            result = self.run_cli(
                "grammar",
                "migrate",
                "--native",
                str(native),
                "--input",
                str(input_path),
                "--out",
                str(output),
                env={"GAGP_CAPTURE": str(capture)},
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                json.loads(capture.read_text(encoding="utf-8")),
                ["--input", str(input_path), "--out", str(output)],
            )


if __name__ == "__main__":
    unittest.main()
