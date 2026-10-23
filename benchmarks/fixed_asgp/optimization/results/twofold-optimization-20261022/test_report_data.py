"""Partial reports retain failed execution costs and refuse target attainment."""
import unittest

import report_data


class EvidenceStub:
    def stage(self,name,spec):
        if name=='formal-baseline':
            return dict(jobs={'sum_of_elements.p1024.s0.g8.r0':dict(status='failed',process_s=7.)})
        if name=='formal-full-native-baseline':
            return dict(jobs={'sum_of_elements.p1024.baseline.final.full-native':dict(
                status='failed',process_s=11.,task='sum_of_elements',population=1024,variant='baseline')})
        return dict(jobs={})


class TestReportData(unittest.TestCase):
    def test_failed_costs_and_missing_rows(self):
        freeze=dict(tasks=['sum_of_elements'],populations=[1024],generations=8,repetitions=2,
                    variants=dict(baseline={},candidate={}))
        result=report_data.read_speed('formal',freeze,EvidenceStub())
        self.assertEqual(len(result['runs']),4)
        self.assertEqual(len(result['native_audits']),2)
        self.assertEqual(result['costs']['search_process_s'],7.)
        self.assertEqual(result['costs']['native_audit_process_s'],11.)
        self.assertEqual(result['costs']['native_cpu_program_executions'],0)
        self.assertEqual([r['status'] for r in result['runs']],['failed','missing','missing','missing'])
        self.assertFalse(result['summary']['all_primary_targets_reached'])


if __name__=='__main__':unittest.main()
