import copy
import unittest

import full_population_audit_shared_policy as policy
import shared_cpu_reference


class TestSnapshotPolicy(unittest.TestCase):
    def fixture(self):
        return dict(variants=dict(baseline=dict(extra={'GAGP_GPU_STAGE_STORAGE': '0'}),
            candidate=dict(extra={'GAGP_GPU_STAGE_STORAGE': '1',
                                  'GAGP_FUSED_COLD_EXECUTABLE': '0'})),
            native_audit_neutral_extra={'GAGP_FUSED_COLD_EXECUTABLE': '0'})

    def test_both_snapshots_get_same_disabled_flag_search_is_unchanged(self):
        freeze = self.fixture()
        original = copy.deepcopy(freeze)
        result = policy.snapshot_policy(freeze)
        self.assertEqual(freeze, original)
        a, b = [shared_cpu_reference.cpu_settings({}, result['variants'][v]['extra'])
                for v in ('baseline', 'candidate')]
        self.assertEqual(a, b)
        self.assertEqual(a['GAGP_FUSED_COLD_EXECUTABLE'], '0')

    def test_enabled_or_unknown_flags_cannot_be_normalized(self):
        for extra in ({'GAGP_FUSED_COLD_EXECUTABLE': '1'}, {'GAGP_HOST_THREADS': '1'}):
            freeze = self.fixture()
            freeze['native_audit_neutral_extra'] = extra
            with self.assertRaises(ValueError):
                policy.snapshot_policy(freeze)
        freeze = self.fixture()
        freeze['variants']['candidate']['extra']['GAGP_FUSED_COLD_EXECUTABLE'] = '1'
        with self.assertRaises(ValueError):
            policy.snapshot_policy(freeze)
