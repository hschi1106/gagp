import unittest

import formal_after_target


class TestActualTargetGate(unittest.TestCase):
    def test_admission_is_distinct_from_achieving_the_requested_target(self):
        dev = dict(status="requested_phase_complete", task_count=30, performance_admitted=True,
                   binary="immutable", binary_sha256="a"*64, candidate_extra={})
        conf = dict(status="requested_phase_complete", comparisons=90, geometric_mean=1.04,
                    binary_sha256="a"*64, candidate_extra={})
        self.assertFalse(formal_after_target.target_ready(dev, conf))
        self.assertTrue(formal_after_target.target_ready(dev, dict(conf, geometric_mean=1.31)))


if __name__ == "__main__":
    unittest.main()
