import copy
import unittest
import runtime_forecast as f


class TestRuntimeForecast(unittest.TestCase):
    def test_fixed_search_cost_is_not_multiplied_with_generations(self):
        doc=dict(evolve_wall_ms=1300,generations=[dict(generation_ms=100)]*8)
        self.assertAlmostEqual(f.project_search(doc,2.,16),2.8)
        self.assertAlmostEqual(f.project_search(doc,2.,8),2.)
        with self.assertRaises(ValueError):f.project_search(doc,.5,16)

    def historical(self):
        return [dict(task=task,population=pop,variant=variant,repetition=0,status='ok',full_final_cpu_audit='True',
                     generations=8,cpu_audit_program_count=pop,cpu_audit_s=cost)
                for task in ['a','b'] for pop in [1024,16384]
                for variant,cost in [('gpu_original',1.),('gpu_improved',2.)]]

    def test_full_native_forecast_pays_both_variants_at_every_population(self):
        rows=self.historical();self.assertEqual(f.full_audit_cost(rows,['a','b'],[1024,16384],8),16.)
        self.assertEqual(f.full_audit_cost(rows,['a','b'],[1024,16384],32),64.)
        rows[0]['cpu_audit_program_count']=16
        with self.assertRaises(ValueError):f.full_audit_cost(rows,['a','b'],[1024,16384],8)

    def test_partial_old_cost_rows_cannot_be_extrapolated_as_full_coverage(self):
        rows=self.historical();rows.pop()
        with self.assertRaises(ValueError):f.full_audit_cost(rows,['a','b'],[1024,16384],8)

    def fixture(self):
        training=[dict(task='a',variant=v,status='ok',generations=64,population=1024,cases=100,
                       process_s=10.,evaluation_s=2.,native_train_s=3.) for v in ['baseline','candidate']]
        timing=[dict(task='a',variant=v,heldout=False,training_cases=100,common_training_s=4.,
                     common_repeat1000_s=5.,native_repeat1000_s=6.) for v in ['baseline','candidate']]
        prep=[dict(task='a',heldout=False,seed=0,freeze_s=1.,prepare_s=2.)]
        return training,timing,prep

    def test_quality_cost_includes_both_splits_all_champions_and_preparation(self):
        a,b,c=self.fixture();d=f.quality_seed_cost(a,b,c,['a'])
        self.assertEqual(d['search_process_s'],24.)
        self.assertEqual(d['common_and_native_audit_process_s'],50.)
        self.assertEqual(d['preparation_process_s'],3.)
        self.assertEqual(d['total_s'],77.)

    def test_fresh_heldout_or_missing_costs_are_rejected(self):
        a,b,c=self.fixture();b[0]['heldout']=True
        with self.assertRaises(ValueError):f.quality_seed_cost(a,b,c,['a'])
        a,b,c=self.fixture();a.pop()
        with self.assertRaises(ValueError):f.quality_seed_cost(a,b,c,['a'])


if __name__=='__main__':unittest.main()
