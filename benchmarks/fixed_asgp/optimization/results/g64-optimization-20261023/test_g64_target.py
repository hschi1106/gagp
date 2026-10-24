"""The new G64 target is 1.3x with complete coverage, not the old 2x gate."""
import unittest

import study_statistics


class TestG64Target(unittest.TestCase):
    def test_requested_target_and_complete_repetitions(self):
        rows = [dict(task="fixture", population=1024, repetition=r, statuses=["ok", "ok"],
                     speedup=1.4, final_descriptors_equal=True,
                     champion_ast_fitness_equal=True, final_export_equal=True) for r in range(3)]
        result = study_statistics.speed_summary(rows, ["fixture"], [1024], 3)
        self.assertEqual(result["target"], 1.3)
        self.assertTrue(result["all_primary_targets_reached"])
        self.assertFalse(study_statistics.speed_summary(rows, ["fixture"], [1024], 3, target=1.5)["all_primary_targets_reached"])
        self.assertFalse(study_statistics.speed_summary(rows[:2], ["fixture"], [1024], 3)["all_primary_targets_reached"])

    def test_empty_or_duplicate_grid_cannot_pass(self):
        for tasks, populations, repetitions, target in [([], [1024], 3, 1.3),
                (["fixture"], [], 3, 1.3), (["fixture", "fixture"], [1024], 3, 1.3),
                (["fixture"], [1024, 1024], 3, 1.3), (["fixture"], [1024], 0, 1.3),
                (["fixture"], [1024], 3, float("nan"))]:
            with self.assertRaises(ValueError):
                study_statistics.speed_summary([], tasks, populations, repetitions, target)


if __name__ == "__main__":
    unittest.main()
