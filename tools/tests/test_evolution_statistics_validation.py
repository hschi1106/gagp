"""Validate frozen evolution-statistics decision accounting."""
import unittest

from gagp_tools.experiments.capture_evolution_statistics import validate_host_decisions, validate_gpu_decisions


class TestEvolutionStatisticsValidation(unittest.TestCase):
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

if __name__ == "__main__":
    unittest.main()
