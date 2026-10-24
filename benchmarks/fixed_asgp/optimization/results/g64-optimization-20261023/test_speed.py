import tempfile
from pathlib import Path
import unittest

import serial
import speed


class TestG64SpeedControls(unittest.TestCase):
    def fixture(self, folder):
        jobs = {}
        for variant, seconds in [("baseline", 2), ("candidate", 1), ("asgp", 10)]:
            output = folder / (variant + ".json")
            history = folder / (variant + ".history.json")
            export = folder / (variant + ".export.json")
            serial.save(output, dict(final_population=[dict(fitness=0, wrong=0, nodes=1)] * 1024))
            serial.save(history, dict(history=[dict(ast={"fixture": 1}, fitness=0)] * 64))
            serial.save(export, dict(programs=[{"fixture": 1}] * 1024))
            jobs[variant] = dict(task="fixture", seed=35, repetition=0, cases=200, status="ok",
                                 search_s=seconds, output=str(output), history=str(history),
                                 export=str(export), export_sha256=serial.sha(export))
        return jobs

    def test_fresh_denominators_and_ratio_direction(self):
        with tempfile.TemporaryDirectory() as temporary:
            row = speed.compare(self.fixture(Path(temporary)))
        self.assertEqual(row["speedup"], 2)
        self.assertEqual(row["asgp_over_baseline"], 5)
        self.assertEqual(row["asgp_over_candidate"], 10)
        self.assertTrue(row["final_descriptors_equal"])
        self.assertTrue(row["champion_ast_fitness_equal"])

    def test_failed_asgp_is_unknown_denominator_not_a_removed_task(self):
        with tempfile.TemporaryDirectory() as temporary:
            jobs = self.fixture(Path(temporary))
            jobs["asgp"]["status"] = "budget_not_run"
            row = speed.compare(jobs)
        self.assertEqual(row["asgp_status"], "budget_not_run")
        self.assertEqual(row["speedup"], 2)
        self.assertIsNone(row["asgp_over_candidate"])

    def test_failed_candidate_cannot_supply_a_paired_speedup(self):
        with tempfile.TemporaryDirectory() as temporary:
            jobs = self.fixture(Path(temporary))
            jobs["candidate"]["status"] = "interrupted"
            row = speed.compare(jobs)
        self.assertEqual(row["statuses"], ["ok", "interrupted"])
        self.assertIsNone(row["speedup"])
        self.assertFalse(row["final_descriptors_equal"])

    def test_asgp_is_the_same_population_cases_and_generations(self):
        doc = dict(mode="asgp_1t", population=1024, cases=200,
                   final_population=[{}] * 1024, final_best=-1, evolve_wall_ms=1,
                   generations=[dict(expected_program_cases=204800)] * 64,
                   reproduction_profile="asgp-fixed-native-elite1")
        speed.validate(doc, "asgp_1t", 200)
        doc["generations"].pop()
        with self.assertRaises(AssertionError):
            speed.validate(doc, "asgp_1t", 200)


if __name__ == "__main__":
    unittest.main()
