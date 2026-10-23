import copy
import math
import unittest

import study_statistics


class TestScientificGuards(unittest.TestCase):
    def test_exact_binomial_edges(self):
        lo,hi=study_statistics.clopper_pearson(0,10)
        self.assertEqual(lo,0)
        self.assertAlmostEqual(hi,1-.025**.1,places=12)
        a,b=study_statistics.clopper_pearson(10,10)
        self.assertAlmostEqual(a,.025**.1,places=12)
        self.assertEqual(b,1)
        self.assertAlmostEqual(study_statistics.binomial_cdf(1,2,.5),.75)

    def test_agreement_is_not_zero_uncertainty(self):
        result=study_statistics.paired_success([True]*10,[True]*10)
        self.assertLess(result['difference_ci95'][0],-.05)
        self.assertFalse(result['supports_noninferiority'])
        observed=study_statistics.paired_success([True]*300,[True]*300)
        self.assertGreater(observed['difference_ci95'][0],-.05)
        self.assertEqual(observed['difference'],0)

    def test_discordance_direction_and_pairing(self):
        result=study_statistics.paired_success([True,False,True,False],[False,True,False,False])
        self.assertEqual((result['gained'],result['lost'],result['difference']),(1,2,-.25))
        with self.assertRaises(ValueError):study_statistics.paired_success([True],[])

    def valid_rows(self):
        return [dict(task=t,population=p,repetition=r,statuses=['ok','ok'],speedup=2.1,
            final_descriptors_equal=True,champion_ast_fitness_equal=True,final_export_equal=True)
            for t in ('a','b') for p in (1024,16384) for r in range(2)]

    def test_full_coverage_required_for_target(self):
        rows=self.valid_rows()
        result=study_statistics.speed_summary(rows,['a','b'],[1024,16384],2)
        self.assertTrue(result['all_primary_targets_reached'])
        result=study_statistics.speed_summary(rows[:-1],['a','b'],[1024,16384],2)
        self.assertFalse(result['all_primary_targets_reached'])
        self.assertEqual(result['groups']['16384']['qualified_cells'],1)
        self.assertFalse(result['groups']['16384']['full_coverage'])

    def test_failures_or_fidelity_changes_remain_failures(self):
        for field,value in [('statuses',['ok','budget_not_run']),('champion_ast_fitness_equal',False),('speedup',math.nan)]:
            rows=self.valid_rows();rows[0][field]=value
            result=study_statistics.speed_summary(rows,['a','b'],[1024,16384],2)
            self.assertFalse(result['all_primary_targets_reached'])
            self.assertEqual(result['groups']['1024']['qualified_cells'],1)
        rows=self.valid_rows()
        with self.assertRaises(ValueError):study_statistics.speed_summary(rows+[copy.copy(rows[0])],['a','b'],[1024,16384],2)



class TestSeedClusterInference(unittest.TestCase):
    def rows(self,n):
        return [dict(task=task,seed=seed,complete_pair=True,baseline_success=True,candidate_success=True)
                for task in ('a','b') for seed in range(n)]

    def test_twenty_agreeing_seeds_are_underpowered(self):
        result=study_statistics.clustered_quality_bound(self.rows(20),['a','b'],list(range(20)))
        self.assertFalse(result['supports_noninferiority'])
        self.assertAlmostEqual(result['upper_bad_seed_probability'],1-.025**(1/20),places=12)
        result=study_statistics.clustered_quality_bound(self.rows(80),['a','b'],list(range(80)))
        self.assertTrue(result['supports_noninferiority'])

    def test_shared_seed_losses_do_not_become_independent_trials(self):
        rows=self.rows(80);rows[0]['candidate_success']=False;rows[80]['candidate_success']=False
        result=study_statistics.clustered_quality_bound(rows,['a','b'],list(range(80)))
        self.assertEqual((result['bad_seed_count'],result['observed_lost_pairs']),(1,2))
        self.assertEqual(result['seed_clusters'],80)
        rows.pop()
        missing=study_statistics.clustered_quality_bound(rows,['a','b'],list(range(80)))
        self.assertEqual(missing['unknown_pairs'],1)
        self.assertEqual(missing['bad_seed_count'],2)

    def test_duplicate_and_foreign_pairs_rejected(self):
        rows=self.rows(20)
        with self.assertRaises(ValueError):study_statistics.clustered_quality_bound(rows+[rows[0]],['a','b'],list(range(20)))
        with self.assertRaises(ValueError):study_statistics.clustered_quality_bound(rows,['a'],list(range(20)))


if __name__=='__main__':unittest.main()
