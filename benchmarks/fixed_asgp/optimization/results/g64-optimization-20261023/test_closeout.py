import unittest
import closeout_after_formal


class TestFinalExclusion(unittest.TestCase):
    def test_actual_live_child_blocks_even_completed_controller(self):
        state = dict(status="validation_stages_finished", pid=10, phases={"search": dict(pid=11)})
        with self.assertRaises(AssertionError):
            closeout_after_formal.assert_quiescent(state, [], lambda pid: pid == 11)

    def test_interrupted_parent_cannot_hide_live_sample(self):
        state = dict(status="stopped", pid=10)
        manifests = [dict(controller_pid=12, jobs={"sample": dict(status="running", pid=13)})]
        with self.assertRaises(AssertionError):
            closeout_after_formal.assert_quiescent(state, manifests, lambda pid: pid == 13)

    def test_declared_budget_failures_allow_safe_closeout(self):
        state = dict(status="stopped", pid=10)
        manifests = [dict(controller_pid=12, jobs={"sample": dict(status="budget_not_run")})]
        closeout_after_formal.assert_quiescent(state, manifests, lambda pid: False)

    def test_unresolved_rows_require_explicit_inspection(self):
        state = dict(status="stopped", pid=10)
        manifests = [dict(jobs={"sample": dict(status="running", pid=13)})]
        with self.assertRaises(AssertionError):
            closeout_after_formal.assert_quiescent(state, manifests, lambda pid: False)
