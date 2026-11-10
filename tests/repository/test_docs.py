"""Checks that the documentation stays consistent with the repository."""
from __future__ import annotations

import json
import re
import unittest
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[2]
MARKDOWN_LINK = re.compile(r"(?<!!)\[[^]]*\]\(([^)]+)\)")
TABLE_LINK = re.compile(r"^\| \[[^]]+\]\(([^)#]+\.md)\) \|", re.MULTILINE)
LAYOUT_PATH = re.compile(r"^\| `([^`]+)` \|", re.MULTILINE)
JSON_FENCE = re.compile(r"^```json\s*\n(.*?)^```", re.MULTILINE | re.DOTALL)
OPTION_ROW = re.compile(r"^\| `(--[a-z-]+)", re.MULTILINE)
ENV_VAR = re.compile(r"\bGAGP_[A-Z0-9_]+\b")


def maintained_markdown() -> list[Path]:
    files = set(ROOT.glob("*.md"))
    for directory in ("docs", "spec", "benchmarks", "configs", "examples", ".agents"):
        files.update((ROOT / directory).rglob("*.md"))
    return sorted(files)


def section(text: str, heading: str) -> str:
    start = text.index(heading)
    end = text.find("\n## ", start + len(heading))
    return text[start:] if end < 0 else text[start:end]


class TestDocumentation(unittest.TestCase):
    def test_relative_links_resolve(self) -> None:
        failures = []
        for document in maintained_markdown():
            for raw in MARKDOWN_LINK.findall(document.read_text(encoding="utf-8")):
                target = raw.strip().strip("<>")
                if target.startswith(("#", "http://", "https://", "mailto:")):
                    continue
                path = unquote(target.split("#", 1)[0])
                if path and not (document.parent / path).exists():
                    failures.append(f"{document.relative_to(ROOT)} -> {target}")
        self.assertEqual([], failures, "broken local Markdown links")

    def test_indexes_cover_every_document(self) -> None:
        docs_index = ROOT / "docs" / "README.md"
        indexed = {(docs_index.parent / target).resolve()
                   for target in TABLE_LINK.findall(docs_index.read_text(encoding="utf-8"))}
        expected = {path.resolve() for path in (ROOT / "docs").rglob("*.md")} - {docs_index.resolve()}
        self.assertEqual(sorted(map(str, expected)), sorted(map(str, indexed - {docs_index.resolve()})))

        spec_index = ROOT / "spec" / "README.md"
        specs = {(spec_index.parent / target).resolve()
                 for target in TABLE_LINK.findall(spec_index.read_text(encoding="utf-8"))}
        expected = {path.resolve() for path in (ROOT / "spec").glob("*.md")} - {spec_index.resolve()}
        self.assertEqual(sorted(map(str, expected)), sorted(map(str, specs)))

    def test_repository_layout_paths_exist(self) -> None:
        layout = (ROOT / "docs" / "reference" / "repository-layout.md").read_text(encoding="utf-8")
        paths = LAYOUT_PATH.findall(layout)
        self.assertGreater(len(paths), 15)
        self.assertEqual([], [path for path in paths if not (ROOT / path).exists()])

    def test_json_examples_parse(self) -> None:
        failures = []
        for document in maintained_markdown():
            for index, body in enumerate(JSON_FENCE.findall(document.read_text(encoding="utf-8")), 1):
                try:
                    json.loads(body)
                except json.JSONDecodeError as error:
                    failures.append(f"{document.relative_to(ROOT)} fence {index}: {error}")
        self.assertEqual([], failures, "invalid JSON examples")

    def test_run_options_match_the_parser(self) -> None:
        parser = (ROOT / "cpp" / "src" / "cli" / "run.cpp").read_text(encoding="utf-8")
        parsed = set(re.findall(r'arg == "(--[a-z-]+)"', parser))
        reference = (ROOT / "docs" / "reference" / "cli.md").read_text(encoding="utf-8")
        documented = set(OPTION_ROW.findall(section(reference, "## `gagp run`")))
        self.assertEqual(parsed, documented)

    def test_environment_variables_match_the_code(self) -> None:
        read = set()
        for path in (ROOT / "cpp" / "src").rglob("*"):
            if path.suffix in {".cpp", ".cu", ".hpp", ".cuh"}:
                read.update(re.findall(r'getenv\("(GAGP_[A-Z0-9_]+)"\)', path.read_text(encoding="utf-8")))
        reference = (ROOT / "docs" / "reference" / "cli.md").read_text(encoding="utf-8")
        self.assertEqual(read, set(ENV_VAR.findall(section(reference, "## Environment variables"))))
        cmake_options = set(re.findall(r"^option\((GAGP_[A-Z0-9_]+)",
                                       (ROOT / "cpp" / "CMakeLists.txt").read_text(encoding="utf-8"),
                                       re.MULTILINE))
        failures = [f"{document.relative_to(ROOT)}: {name}"
                    for document in maintained_markdown()
                    for name in set(ENV_VAR.findall(document.read_text(encoding="utf-8"))) - read - cmake_options]
        self.assertEqual([], failures, "documented variables that neither the code nor CMake reads")

    def test_removed_programs_are_not_documented(self) -> None:
        removed = re.compile(r"gagp_(?:evolve|generate|grammar|vm)_cli|gagp-tools|gagp_tools"
                             r"|engine_profile|--repro-backend|tools/README")
        # VERSION.md records what each release removed.
        failures = [f"{document.relative_to(ROOT)}: {match}"
                    for document in maintained_markdown() if document.name != "VERSION.md"
                    for match in removed.findall(document.read_text(encoding="utf-8"))]
        self.assertEqual([], failures)

    def test_identity(self) -> None:
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        self.assertTrue(readme.startswith("# GAGP\n"))
        self.assertIn("README.zh-TW.md", readme)
        self.assertIn("Current release: 3.0.1", (ROOT / "VERSION.md").read_text(encoding="utf-8"))
        presets = json.loads((ROOT / "cpp" / "CMakePresets.json").read_text(encoding="utf-8"))
        names = {preset["name"] for preset in presets["configurePresets"]}
        self.assertTrue({"debug", "release", "cuda-parity", "sanitizer", "fuzz"} <= names)


if __name__ == "__main__":
    unittest.main()
