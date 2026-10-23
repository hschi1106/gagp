import copy
import unittest

import report_validation2 as validation


class TestReportSnapshotCommands(unittest.TestCase):
    def fixture(self):
        freeze = dict(variants={'baseline': dict(binary_sha256='sha', extra={})},
                      native_audit_neutral_extra={'GAGP_FUSED_COLD_EXECUTABLE': '0'})
        job = dict(command=['bench', 'measure', 'source', 'prepared', 'grammar', '4', 'snapshot', 'out'],
                   variant='baseline', status='ok', population=1024, cases=1024,
                   native_cpu_executed_programs=4,
                   extra={'GAGP_FUSED_COLD_EXECUTABLE': '0', 'GAGP_BM_CASE_COUNT': '1024',
                          'GAGP_SNAPSHOT_NATIVE_OWNER': '1', 'GAGP_SNAPSHOT_CPU': '1'})
        return job, {'binary_sha256': 'sha'}, freeze

    def test_native_snapshot_uses_unique_count_not_logical_population(self):
        validation.validate_measure_command(*self.fixture())

    def test_gpu_snapshot_keeps_proved_reference_and_zero_new_cpu_executions(self):
        job, manifest, freeze = self.fixture()
        job.update(native_cpu_executed_programs=0, native_cpu_reference_programs=4,
                   cpu_reference={'origin': 'actual paired job'}, cpu_reference_verified=True)
        del job['extra']['GAGP_SNAPSHOT_CPU']
        validation.validate_measure_command(job, manifest, freeze)

    def test_search_population_and_recipe_still_checked(self):
        job, manifest, freeze = self.fixture()
        job['command'][6] = 'search_gpu_overlap'
        job['command'][5] = '1024'
        validation.validate_measure_command(job, manifest, freeze)
        job['command'][5] = '4'
        with self.assertRaises(AssertionError):
            validation.validate_measure_command(job, manifest, freeze)

    def test_inconsistent_snapshot_count_policy_or_mode_is_rejected(self):
        for edit in (lambda job: job['command'].__setitem__(5, '1024'),
                     lambda job: job['command'].__setitem__(6, 'cpu_eval'),
                     lambda job: job['extra'].pop('GAGP_FUSED_COLD_EXECUTABLE'),
                     lambda job: job['extra'].update(GAGP_HOST_THREADS='8')):
            job, manifest, freeze = self.fixture()
            edit(job)
            with self.assertRaises(AssertionError):
                validation.validate_measure_command(job, manifest, freeze)
