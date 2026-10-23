"""Interrupted snapshots cannot be promoted to native execution evidence."""
import copy
import unittest

import report_validation4


class TestInterruptedAudit(unittest.TestCase):
    def fixture(self):
        freeze = {'variants': {'baseline': {'binary_sha256': 'pinned', 'extra': {'OPTION': '0'}}},
                  'native_audit_neutral_extra': {'GAGP_FUSED_COLD_EXECUTABLE': '0'}}
        manifest = {'binary_sha256': 'pinned'}
        job = dict(command=['bench','measure','source','prepared','grammar','14331','snapshot','out'],
                   variant='baseline',status='interrupted',cases=1024,
                   extra={'OPTION':'0','GAGP_FUSED_COLD_EXECUTABLE':'0',
                          'GAGP_BM_CASE_COUNT':'1024','GAGP_SNAPSHOT_NATIVE_OWNER':'1','GAGP_SNAPSHOT_CPU':'1'},
                   native_execution_coverage_unknown=True,process_s_is_lower_bound=True,
                   native_fidelity_pass=False)
        return job, manifest, freeze

    def test_unknown_execution_retains_command_proof_without_invented_coverage(self):
        job, manifest, freeze = self.fixture()
        before = copy.deepcopy(job)
        report_validation4.validate_measure_command(job, manifest, freeze)
        self.assertEqual(job, before)
        for changes in ({'native_fidelity_pass':True}, {'native_cpu_executed_programs':14331},
                        {'native_execution_coverage_unknown':False}, {'status':'ok'}):
            with self.assertRaises((AssertionError,KeyError)):
                report_validation4.validate_measure_command(dict(job,**changes),manifest,freeze)

    def test_interruption_does_not_bypass_binary_or_snapshot_settings(self):
        job, manifest, freeze = self.fixture()
        for field, value in [('GAGP_SNAPSHOT_CPU','0'),('OPTION','1'),('NEW_OPTION','1')]:
            altered=copy.deepcopy(job);altered['extra'][field]=value
            with self.assertRaises(AssertionError):
                report_validation4.validate_measure_command(altered,manifest,freeze)
        with self.assertRaises(AssertionError):
            report_validation4.validate_measure_command(job,{'binary_sha256':'wrong'},freeze)
