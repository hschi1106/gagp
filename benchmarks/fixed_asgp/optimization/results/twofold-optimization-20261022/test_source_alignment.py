import unittest

from align_selected_source import plan_alignment


class TestSourceAlignment(unittest.TestCase):
    def test_exact_older_winner_restores_changed_and_removes_only_untracked(self):
        prior = {"cpp/main.cpp": "new", "cpp/rejected.hpp": "rejected", "cpp/same.hpp": "same"}
        selected = {"cpp/main.cpp": "winner", "cpp/same.hpp": "same", "cpp/winner.hpp": "added"}
        plan = plan_alignment(prior, dict(prior), selected, ["cpp/main.cpp", "cpp/same.hpp"])
        self.assertEqual(plan, dict(copy=["cpp/main.cpp", "cpp/winner.hpp"],
                                    remove=["cpp/rejected.hpp"], unchanged=["cpp/same.hpp"]))

    def test_unknown_user_file_refuses_before_any_mutation(self):
        with self.assertRaisesRegex(ValueError, "differs"):
            plan_alignment({"cpp/main.cpp": "a"}, {"cpp/main.cpp": "a", "cpp/user.cpp": "user"},
                           {"cpp/main.cpp": "b"}, ["cpp/main.cpp"])

    def test_changed_user_file_refuses_before_any_mutation(self):
        with self.assertRaisesRegex(ValueError, "differs"):
            plan_alignment({"cpp/main.cpp": "a"}, {"cpp/main.cpp": "user"},
                           {"cpp/main.cpp": "b"}, ["cpp/main.cpp"])

    def test_tracked_removal_requires_manual_inspection(self):
        prior = {"cpp/main.cpp": "a", "cpp/keep.cpp": "keep"}
        with self.assertRaisesRegex(ValueError, "tracked"):
            plan_alignment(prior, prior, {"cpp/main.cpp": "b"}, list(prior))

    def test_paths_outside_cpp_rejected(self):
        for path in ["/cpp/main.cpp", "cpp/../presentations/user.typ", "presentations/user.typ"]:
            with self.subTest(path=path), self.assertRaisesRegex(ValueError, "escapes"):
                plan_alignment({"cpp/main.cpp": "a"}, {"cpp/main.cpp": "a"}, {path: "b"}, [])

    def test_empty_selected_map_rejected(self):
        with self.assertRaisesRegex(ValueError, "complete"):
            plan_alignment({"cpp/main.cpp": "a"}, {"cpp/main.cpp": "a"}, {}, [])


if __name__ == "__main__":
    unittest.main()
