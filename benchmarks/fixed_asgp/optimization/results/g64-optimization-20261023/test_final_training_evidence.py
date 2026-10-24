import unittest
import final_training_evidence


class TestTrainingVersusGoal(unittest.TestCase):
    def fixture(self):
        tasks = ["task" + str(i) for i in range(30)]
        d = dict(status="requested_phase_complete", task_count=30, performance_admitted=True,
                 binary="/bench", binary_sha256="abc", candidate_extra={"flag": "1"})
        c = dict(status="requested_phase_complete", comparisons=90, geometric_mean=1.062,
                 binary_sha256="abc", candidate_extra={"flag": "1"})
        rows = [dict(task=t, seed=s, full_final_ast_equal=True, final_descriptors_equal=True, champions_equal=True)
                for t in tasks for s in (0, 1, 2)]
        native = {v: dict(status="requested_phase_complete", jobs={t+str(s): dict(task=t, seed=s, split="train",
            status="ok", native_fidelity_pass=True, native_mismatches=0, native_program_observations=65)
            for t in tasks for s in (1, 2)}) for v in ("baseline", "candidate")}
        return d, c, rows, native, tasks

    def test_admitted_candidate_does_not_become_a_met_performance_goal(self):
        evidence = final_training_evidence.verify(*self.fixture())
        self.assertFalse(evidence["development_target_met"])
        self.assertEqual(evidence["performance_target"], 1.3)
        self.assertEqual(evidence["native_training_calls"], 120)

    def test_missing_or_duplicate_seed_rows_are_rejected(self):
        d, c, rows, native, tasks = self.fixture()
        rows[-1] = dict(rows[0])
        with self.assertRaises(AssertionError):
            final_training_evidence.verify(d, c, rows, native, tasks)

    def test_native_failure_cannot_be_hidden_by_completed_controller(self):
        d, c, rows, native, tasks = self.fixture()
        next(iter(native["candidate"]["jobs"].values()))["native_fidelity_pass"] = False
        with self.assertRaises(AssertionError):
            final_training_evidence.verify(d, c, rows, native, tasks)

    def test_final_champion_only_is_not_generation_coverage(self):
        d, c, rows, native, tasks = self.fixture()
        next(iter(native["candidate"]["jobs"].values()))["native_program_observations"] = 1
        with self.assertRaises(AssertionError):
            final_training_evidence.verify(d, c, rows, native, tasks)
