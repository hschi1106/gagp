import copy
import unittest

import private_layout_pilot5


class TestPrivateParentRecipe(unittest.TestCase):
    def test_only_complete_global_recipe_can_be_selected(self):
        first = dict(status="requested_phase_complete", task_count=30,
                     arms=dict(ordinary={"recipe": "a"}), candidate_extra={"recipe": "b"},
                     geometric_means=dict(ordinary=1.2, candidate=1.1))
        second = dict(status="requested_phase_complete", task_count=0, coarse_geometric_means=dict(candidate=9))
        value, study, variant, extra = private_layout_pilot5.parent_recipe(dict(a=first, b=second))
        self.assertEqual((value, study, variant, extra), (1.2, "a", "ordinary", {"recipe": "a"}))
        bad = copy.deepcopy(first)
        bad["status"] = "stopped"
        with self.assertRaises(AssertionError):
            private_layout_pilot5.parent_recipe(dict(a=bad))


if __name__ == "__main__":
    unittest.main()
