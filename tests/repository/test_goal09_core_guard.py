from __future__ import annotations

import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class TestGoal09CoreGuard(unittest.TestCase):
    def production_sources(self) -> list[Path]:
        roots = (
            ROOT / "cpp" / "include" / "gagp",
            ROOT / "cpp" / "src" / "evolution",
            ROOT / "cpp" / "src" / "runtime",
            ROOT / "cpp" / "src" / "cli",
            ROOT / "cpp" / "src" / "serialization",
        )
        result: list[Path] = []
        for root in roots:
            for path in root.rglob("*"):
                if path.suffix not in {".h", ".hpp", ".cuh", ".cpp", ".cu"}:
                    continue
                relative = path.relative_to(ROOT).as_posix()
                if "/migration/" in relative or "/transition/" in relative:
                    continue
                result.append(path)
        return sorted(set(result))

    def test_specialized_dispatch_is_absent_from_production(self) -> None:
        forbidden = re.compile(
            r"NodeKind::(?:MAP_LIST|FILTER_LIST|LINEAR_REC|ASGP_DC|ASGP_DP1D|ASGP_DP2D|DP[12]_)"
            r"|Opcode::Asgp"
            r"|\b(?:LinearRecBinders|AsgpDcBinders|AsgpDp1dSpec|AsgpDp2dSpec|DAsgpTables)\b"
            r"|\b(?:ReproductionContractMode|GrammarConfig)\b"
            r"|\b(?:scheme_kind|phase_name|dp_dependency_arity)\b"
        )
        failures: list[str] = []
        for path in self.production_sources():
            for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                if forbidden.search(line):
                    failures.append(f"{path.relative_to(ROOT)}:{number}: {line.strip()}")
        self.assertEqual([], failures)

    def test_retired_production_modules_are_absent(self) -> None:
        retired = (
            "cpp/include/gagp/evolution/grammar_config.hpp",
            "cpp/src/evolution/grammar_config.cpp",
            "cpp/include/gagp/evolution/grammar/config_adapter.hpp",
            "cpp/src/evolution/grammar/config_adapter.cpp",
            "cpp/src/evolution/typed_expr_analysis.cpp",
            "cpp/src/evolution/typed_expr_analysis.hpp",
            "cpp/src/evolution/mutation.cpp",
            "cpp/src/evolution/crossover.cpp",
            "cpp/include/gagp/evolution/transition/bounded_bytecode.hpp",
            "cpp/src/transition/bounded_bytecode.cpp",
            "cpp/src/bench/grammar_migration_bench.cpp",
            "cpp/src/bench/migration_snapshot.cpp",
            "cpp/src/bench/migration_snapshot.hpp",
            "cpp/tests/evolution/test_ast_verify_asgp.cpp",
            "cpp/tests/evolution/test_genome.cpp",
            "cpp/tests/runtime/test_asgp_semantics.cpp",
            "tools/gagp_tools/experiments/capture_cpu_reproduction.py",
            "tools/gagp_tools/experiments/capture_gpu_oracle.py",
            "tools/gagp_tools/experiments/capture_migration_oracle.py",
        )
        self.assertEqual([], [path for path in retired if (ROOT / path).exists()])

    def test_production_targets_do_not_link_migration(self) -> None:
        cmake = (ROOT / "cpp" / "CMakeLists.txt").read_text(encoding="utf-8")
        for target in (
            "gagp_core",
            "gagp_runtime_cpu",
            "gagp_gpu",
            "gagp_grammar",
            "gagp_evolution",
            "gagp_cli_support",
            "gagp_evolve_cli",
            "gagp_generate_cli",
        ):
            links = re.findall(
                rf"target_link_libraries\({target}\s+([^\)]*)\)", cmake, re.DOTALL
            )
            self.assertTrue(all("gagp_transition" not in one for one in links), target)

    def test_native_test_sources_are_registered(self) -> None:
        cmake = (ROOT / "cpp" / "CMakeLists.txt").read_text(encoding="utf-8")
        unregistered: list[str] = []
        for path in (ROOT / "cpp" / "tests").rglob("test_*"):
            if path.suffix not in {".cpp", ".cu", ".py"}:
                continue
            relative = path.relative_to(ROOT / "cpp").as_posix()
            if path.name not in cmake and relative not in cmake:
                unregistered.append(relative)
        self.assertEqual([], sorted(unregistered))

    def test_current_presets_are_v2_definitions(self) -> None:
        for name in ("all", "num_list", "scalar", "sequence", "string", "string_list"):
            path = ROOT / "configs" / "grammar" / f"{name}.json"
            payload = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual("grammar-definition-v2", payload.get("format_version"), name)
            self.assertNotIn("asgp", payload, name)
            self.assertNotIn("expressions", payload, name)

    def test_numeric_holes_stay_reserved(self) -> None:
        opcode = (ROOT / "cpp" / "include" / "gagp" / "core" / "opcode.hpp").read_text(
            encoding="utf-8"
        )
        ast = (ROOT / "cpp" / "include" / "gagp" / "evolution" / "ast_program.hpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("BoundedRegion = 28", opcode)
        self.assertNotRegex(opcode, r"=\s*(?:25|26|27)\s*,")
        self.assertIn("LET_REGION = 71", ast)
        for value in range(53, 71):
            self.assertNotRegex(ast, rf"=\s*{value}\s*,")


if __name__ == "__main__":
    unittest.main()
