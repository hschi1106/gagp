import copy
import unittest

import asgp_audit


class TestAsgpNativeAudit(unittest.TestCase):
    def fixture(self):
        search = dict(generations=[dict(best=-2, best_wrong=1), dict(best=0, best_wrong=0)],
                      final_best=0, final_population=[dict(fitness=-2, wrong=1, nodes=3),
                                                     dict(fitness=0, wrong=0, nodes=5)])
        rows = [dict(fitness=-2, wrong=1, nodes=3, cases=3),
                dict(fitness=0, wrong=0, nodes=5, cases=3),
                dict(fitness=0, wrong=0, nodes=5, cases=3)]
        return rows, search

    def test_unsorted_final_population_and_all_champions(self):
        rows, search = self.fixture()
        result = asgp_audit.validate(rows, search, "train", 3)
        self.assertTrue(result["native_fidelity_pass"])
        self.assertEqual(result["native_program_observations"], 3)
        with self.assertRaises(AssertionError):
            asgp_audit.validate(rows[:-1], search, "train", 3)

    def test_training_fitness_and_wrong_are_separate_checks(self):
        rows, search = self.fixture()
        rows[0]["fitness"] = -3
        rows[1]["wrong"] = 1
        result = asgp_audit.validate(rows, search, "train", 3)
        self.assertFalse(result["native_fidelity_pass"])
        self.assertEqual(result["native_mismatches"]["search_native_fitness"], 1)
        self.assertEqual(result["native_mismatches"]["search_native_wrong"], 1)
        self.assertTrue(asgp_audit.validate(rows, search, "test", 3)["native_fidelity_pass"])

    def test_native_invalid_count_or_nonfinite_is_rejected(self):
        original, search = self.fixture()
        for field, value in [("cases", 2), ("wrong", 4), ("fitness", float("nan"))]:
            rows = copy.deepcopy(original)
            rows[0][field] = value
            with self.assertRaises(AssertionError):
                asgp_audit.validate(rows, search, "train", 3)

    def test_final_export_must_match_an_actual_final_descriptor(self):
        rows, search = self.fixture()
        rows[-1]["nodes"] = 999
        result = asgp_audit.validate(rows, search, "train", 3)
        self.assertFalse(result["native_fidelity_pass"])
        self.assertEqual(result["native_mismatches"]["final_descriptor"], 1)


if __name__ == "__main__":
    unittest.main()
