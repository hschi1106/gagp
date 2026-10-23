import unittest

import short_edit_pilot as pilot


class TestShortEditStudyContract(unittest.TestCase):
    def test_profiles_are_per_generation_without_root_profile(self):
        document = {'generations': [{'execution_profile': 'view'}, {'execution_profile': 'private-short-edit-exact'}]}
        self.assertEqual(pilot.execution_profiles(document), ['view', 'private-short-edit-exact'])

    def test_missing_or_empty_generation_profiles_fail(self):
        for document in [{'generations': []}, {'generations': [{}]}, {'generations': [{'execution_profile': ''}]}]:
            with self.subTest(document=document), self.assertRaises((KeyError, ValueError)):
                pilot.execution_profiles(document)

    def test_five_arms_only_change_two_global_options(self):
        control = pilot.execution_settings('baseline')
        self.assertEqual(control, pilot.execution_settings('ordinary'))
        for variant, short, soa in [('short', '1', '0'), ('soa', '0', '1'), ('short_soa', '1', '1')]:
            expected = dict(control, GAGP_GPU_SHORT_EDIT_DISTANCE=short, GAGP_GPU_PRIVATE_CELL_SOA=soa)
            self.assertEqual(pilot.execution_settings(variant), expected)

    def test_joint_controls_keep_previous_admitted_global_limits(self):
        for variant in pilot.VARIANTS:
            settings = pilot.execution_settings(variant)
            self.assertEqual(settings['GAGP_PHASE_REPRO_THREADS'], '1')
            self.assertEqual(settings['GAGP_GPU_PARENT_FITNESS_CACHE'], '0')
            self.assertEqual(settings['GAGP_FUSED_COLD_EXECUTABLE'], '0')

    def test_unknown_arm_rejected(self):
        with self.assertRaises(ValueError):
            pilot.execution_settings('per-task-winner')
