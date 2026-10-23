import copy
import unittest

import report_attempt_notes as notes


class TestReportAttemptNotes(unittest.TestCase):
    def fixture(self):
        common = {key: {} for key in ('settings', 'inputs', 'variants', 'resources',
            'candidate_selection', 'candidate_source_sha256')}
        common.update(started_epoch=0., deadline_epoch=86400., tasks=['x'], reserve_s=10800,
                      candidate_source_snapshot='fixed')
        speed = dict(common, populations=[1024, 16384], generations=8, repetitions=2, cases=1024)
        quality = dict(common, population=1024, generations=64, seeds=list(range(25, 35)),
                       data_seed=20261022, train_limit=200, test_limit=1000)
        return speed, quality

    def test_only_unchanged_grid_can_be_called_metadata_repair(self):
        speed, quality = self.fixture()
        notes.same_repaired_scope(speed, quality, copy.deepcopy(speed), copy.deepcopy(quality))
        for field, value in [('seeds', [25]), ('data_seed', 0)]:
            parent = copy.deepcopy(quality)
            parent[field] = value
            with self.assertRaises(ValueError):
                notes.same_repaired_scope(speed, quality, copy.deepcopy(speed), parent)
        parent = copy.deepcopy(speed)
        parent['generations'] = 4
        with self.assertRaises(ValueError):
            notes.same_repaired_scope(speed, quality, parent, copy.deepcopy(quality))

    def test_cost_does_not_reclassify_timeout_or_missing_audit(self):
        rows = [dict(status='timeout', native_call_elapsed_s_including_lock_wait=4.,
                     native_controller_cpu_s=1.), dict(status='missing')]
        original = copy.deepcopy(rows)
        result = notes.native_costs(rows)
        self.assertEqual(result['known_elapsed_s'], 4.)
        self.assertEqual(result['elapsed_recorded_calls'], 1)
        self.assertEqual(result['expected_calls'], 2)
        self.assertEqual(rows, original)
        with self.assertRaises(ValueError):
            notes.native_costs([{'native_controller_cpu_s': -1.}])
