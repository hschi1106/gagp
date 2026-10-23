import copy
import unittest
import report_qualification


class TestSpeedNativeQualification(unittest.TestCase):
    def fixture(self):
        rows=[dict(task=task,population=population,repetition=rep,statuses=['ok','ok'],speedup=2.1,
                   final_descriptors_equal=True,champion_ast_fitness_equal=True,final_export_equal=True)
              for task in ['a','b'] for population in [1024,16384] for rep in range(2)]
        audits=[dict(task=task,population=population,variant=variant,status='ok',native_fidelity_pass=True,
                     native_program_observations=population,native_case_observations=population*1024,
                     native_cpu_executed_programs=17,native_cpu_executed_cases=17*1024,
                     native_mismatches={'fitness':0,'wrong':0})
                for task in ['a','b'] for population in [1024,16384] for variant in ['baseline','candidate']]
        return rows,audits

    def run_summary(self,rows,audits):
        return report_qualification.speed_with_native_audits(rows,audits,['a','b'],[1024,16384],2)

    def test_timings_alone_never_qualify_target(self):
        rows,audits=self.fixture();result=self.run_summary(rows,[])
        self.assertFalse(result['all_primary_targets_reached'])
        self.assertTrue(result['groups']['pooled']['timing_target_reached'])
        self.assertEqual(result['groups']['pooled']['timing_qualified_cells'],4)
        self.assertEqual(result['groups']['pooled']['qualified_cells'],0)
        self.assertIsNone(result['groups']['pooled']['observed_geometric_mean'])
        self.assertTrue(all(c['timing_qualified'] and not c['qualified'] for c in result['cells']))
        self.assertTrue(self.run_summary(rows,audits)['all_primary_targets_reached'])

    def test_failure_or_missing_member_coverage_blocks_only_relevant_population_and_pooled(self):
        rows,original=self.fixture()
        for field,value in [('status','failed'),('native_fidelity_pass',False),('native_program_observations',1),
                            ('native_case_observations',1),('native_cpu_executed_cases',0),('native_mismatches',{'wrong':1})]:
            audits=copy.deepcopy(original);audits[0][field]=value;result=self.run_summary(rows,audits)
            self.assertFalse(result['all_primary_targets_reached'])
            self.assertFalse(result['groups']['1024']['target_reached'])
            self.assertTrue(result['groups']['16384']['target_reached'])

    def test_duplicate_unexpected_and_missing_search_rows_are_not_silently_averaged(self):
        rows,audits=self.fixture()
        with self.assertRaises(ValueError):self.run_summary(rows,audits+[audits[0]])
        audits[0]['task']='unexpected'
        with self.assertRaises(ValueError):self.run_summary(rows,audits)
        rows,audits=self.fixture();rows.pop();self.assertFalse(self.run_summary(rows,audits)['all_primary_targets_reached'])


if __name__=='__main__':unittest.main()
