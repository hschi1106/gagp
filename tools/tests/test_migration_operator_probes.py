"""Guard the diagnostic source rewrite against ambiguous or changed inputs."""
import tempfile
import re
import unittest
from pathlib import Path

from gagp_tools.experiments.instrument_migration_operators import generate, insert_event
from gagp_tools.experiments.capture_evolution_statistics import validate_host_decisions, validate_gpu_decisions


class TestMigrationOperatorProbes(unittest.TestCase):
    def test_gpu_probe_blocks_preserve_original_source(self):
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "probes"
            manifest = generate(root, output, gpu=True)
            for relative, details in manifest["sources"].items():
                if not relative.startswith("repro/gpu/"):
                    continue
                restored = re.sub(r"// MIGRATION PROBE BEGIN\n.*?// MIGRATION PROBE END\n", "",
                                  (output / relative).read_text(), flags=re.DOTALL)
                self.assertEqual(restored, Path(details["reference"]).read_text())

    def test_gpu_kernel_and_host_disagreement_is_rejected(self):
        population = {"population_size": 2, "host_operator_decisions": {"pack.decode.device_invalid": 1},
            "gpu_kernel_decisions": {"gpu_kernel." + key: 2 for key in (
                "variation.assembled", "metadata.valid", "mutation.none", "output.valid")}}
        with self.assertRaises(ValueError):
            validate_gpu_decisions(population)
        population["host_operator_decisions"]["pack.decode.device_invalid"] = 0
        validate_gpu_decisions(population)
        del population["gpu_kernel_decisions"]["gpu_kernel.mutation.none"]
        with self.assertRaises(ValueError):
            validate_gpu_decisions(population)

    def test_missing_child_decisions_are_rejected(self):
        for gpu, counts in ((True, {"pack.decode.accepted": 3}),
                            (False, {"backend.mutation.eligible": 3})):
            with self.assertRaises(ValueError):
                validate_host_decisions({"population_size": 4, "host_operator_decisions": counts}, gpu)
        validate_host_decisions({"population_size": 4, "host_operator_decisions": {
            "pack.decode.accepted": 3, "pack.decode.device_invalid": 1}}, True)
        validate_host_decisions({"population_size": 4, "host_operator_decisions": {
            "backend.mutation.eligible": 4, "backend.mutation.selected": 2}}, False)

    def test_ambiguous_or_missing_anchor_is_rejected(self):
        anchor = "if (condition) {\n  return parent;"
        for source in ("", anchor + anchor):
            with self.assertRaises(ValueError):
                insert_event(source, anchor, "fallback")

    def test_failed_reference_validation_leaves_no_partial_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            evolution = root / "reference/cpp/src/evolution"
            evolution.mkdir(parents=True)
            (evolution / "crossover.cpp").write_text("// incompatible reference\n")
            with self.assertRaises(ValueError):
                generate(root / "reference", root / "output")
            self.assertFalse((root / "output").exists())

    def test_all_probes_preserve_source_after_removing_counter_lines(self):
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "probes"
            manifest = generate(root, output)
            for relative, details in manifest["sources"].items():
                generated = (output / relative).read_text().splitlines(keepends=True)
                restored = "".join(line for line in generated[1:]
                                   if "::gagp::migration::record_operator_decision(" not in line)
                self.assertEqual(restored, Path(details["reference"]).read_text())
                self.assertEqual(len(details["events"]), len(set(details["events"])))


if __name__ == "__main__":
    unittest.main()
