import copy
import unittest
import capacity_evidence


class TestCapacityGate(unittest.TestCase):
    def fixture(self):
        rows = [dict(task=t, statuses=["ok", "ok"], population=16384, generations=64,
                     **{k: True for k in capacity_evidence.FIELDS}) for t in capacity_evidence.TASKS]
        stages = {v: dict(status="requested_phase_complete", binary_sha256=v, jobs={
            t + ".p16384.s0.g64": dict(status="ok", task=t, variant=v, population=16384, generations=64,
                output_sha256="a", export_sha256="b", history_sha256="c") for t in capacity_evidence.TASKS})
            for v in ("baseline", "candidate")}
        return dict(status="requested_phase_complete", returncode=0), dict(status="requested_phase_complete", comparisons=rows), stages

    def test_closed_grid_is_observation_not_full_cpu_proof(self):
        d, p, m = self.fixture()
        result = capacity_evidence.check_capacity(d, p, m, "candidate", "baseline")
        self.assertEqual(result["cpu_programs_per_search"], 16)

    def test_completion_does_not_hide_parity_mismatch(self):
        d, p, m = self.fixture()
        p["comparisons"][0]["champions_equal"] = False
        with self.assertRaises(AssertionError):
            capacity_evidence.check_capacity(d, p, m, "candidate", "baseline")

    def test_invalid_or_missing_samples_remain_failures(self):
        for mutation in ("failed", "missing", "binary"):
            d, p, m = self.fixture()
            if mutation == "failed":
                p["comparisons"][0]["statuses"][1] = "invalid_output"
            elif mutation == "missing":
                m["candidate"]["jobs"].pop(next(iter(m["candidate"]["jobs"])))
            else:
                m["candidate"]["binary_sha256"] = "other"
            with self.assertRaises(AssertionError):
                capacity_evidence.check_capacity(d, p, m, "candidate", "baseline")

    def test_budget_skip_cannot_erase_existing_samples(self):
        d, p, m = self.fixture()
        skipped = dict(status="budget_not_run", no_samples=True)
        self.assertEqual(capacity_evidence.check_capacity(skipped, None, {}, "candidate", "baseline")["status"], "not_run_budget")
        with self.assertRaises(AssertionError):
            capacity_evidence.check_capacity(skipped, p, m, "candidate", "baseline")
