import copy
import unittest
import quality_metrics as q


class TestQualityMetrics(unittest.TestCase):
    def fixture(self):
        def score(success):return dict(cases=3,correct_count=3 if success else 2,exact=3 if success else 2,
                                       wrong=0 if success else 1,runtime_errors=0)
        docs={split:dict(cases=3,history=[score(False),score(True)],final=score(True)) for split in ['train','test']}
        common={split:dict(status='ok',cases=3) for split in docs}
        native={split:dict(status='ok',native_fidelity_pass=True,native_program_observations=3,
                           native_case_observations=9,native_cpu_executed_programs=2,native_cpu_executed_cases=6,
                           native_mismatches={'fitness':0,'wrong':0}) for split in docs}
        return dict(status='ok',generations=2),common,docs,native

    def test_gen0_and_subsequent_final_success_are_distinct(self):
        a,b,c,d=self.fixture();out=q.outcome(a,b,c,d)
        self.assertTrue(out['complete']);self.assertFalse(out['splits']['joint']['gen0'])
        self.assertTrue(out['splits']['joint']['new_final'])
        c['train']['history'][0]=copy.deepcopy(c['train']['final']);c['test']['history'][0]=copy.deepcopy(c['test']['final'])
        out=q.outcome(a,b,c,d);self.assertTrue(out['splits']['joint']['gen0'])
        self.assertFalse(out['splits']['joint']['new_final'])

    def test_train_and_test_must_both_solve_the_same_final_selection(self):
        a,b,c,d=self.fixture();c['test']['final'].update(correct_count=2,exact=2,wrong=1)
        out=q.outcome(a,b,c,d);self.assertTrue(out['splits']['train']['final']);self.assertFalse(out['splits']['joint']['final'])

    def test_completed_search_without_every_champion_native_audit_is_unknown(self):
        for key,value in [('status','failed'),('native_program_observations',2),('native_case_observations',8),
                          ('native_cpu_executed_cases',0),('native_fidelity_pass',False),('native_mismatches',{'wrong':1})]:
            a,b,c,d=self.fixture();d['test'][key]=value;self.assertFalse(q.outcome(a,b,c,d)['complete'])

    def test_missing_pair_does_not_disappear_or_narrow_uncertainty(self):
        a,b,c,d=self.fixture();o=q.outcome(a,b,c,d)
        rows=[q.pair('a',1,o,o)]
        out=q.task_summary(rows,'a',[1,2]);self.assertEqual(out['unknown_pairs'],1)
        self.assertEqual(out['identification_range'],[-.5,.5]);self.assertFalse(out['supports_noninferiority'])
        q.study_statistics.clustered_quality_bound(rows,['a'],[1,2])
        rows.append(q.pair('a',2,o,o,execution_fidelity=False))
        self.assertEqual(q.task_summary(rows,'a',[1,2])['unknown_pairs'],1)

    def test_duplicate_pairs_fail_closed(self):
        a,b,c,d=self.fixture();o=q.outcome(a,b,c,d);row=q.pair('a',1,o,o)
        with self.assertRaises(ValueError):q.task_summary([row,row],'a',[1])


if __name__=='__main__':unittest.main()
