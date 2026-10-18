import unittest
import tempfile
import json
from pathlib import Path
import report


class TestCoverageAwarePairs(unittest.TestCase):
    def test_missing_judge_excluded_from_pairs_but_kept_in_planned_yield(self):
        variants=['asgp','gpu_base','gpu_exec','gpu_operator','gpu_guard']
        freeze=dict(tasks=['task'],seeds=[5,6],variants=variants)
        rows=[]
        for seed in freeze['seeds']:
            for variant in variants:
                audited=not(seed==6 and variant=='gpu_operator')
                success=variant=='gpu_operator' and seed==5
                rows.append(dict(task='task',seed=seed,variant=variant,audited=audited,
                    success=success if audited else None,initial_success=False,new_final_success=success if audited else None,
                    lost_initial_success=False,search_status='ok',search_s=2 if variant=='asgp' else 1,
                    final_train_accuracy=.5 if audited else None,final_test_accuracy=.5 if audited else None,
                    verification_fraction=0,cpu_top16_fitness_mismatches=0,cpu_top16_wrong_mismatches=0))
        details,pairs,tests=report.summaries(freeze,rows)
        operator=next(r for r in details if r['variant']=='gpu_operator')
        self.assertEqual((operator['planned'],operator['audited'],operator['successes']),(2,1,1))
        self.assertEqual(operator['operational_success_rate'],.5)
        test=next(r for r in tests if r['a']=='gpu_operator' and r['b']=='gpu_exec')
        self.assertEqual((test['paired_audited'],test['a_only'],test['b_only']),(1,1,0))
        self.assertEqual(test['speed_pairs'],2)
        self.assertEqual(len(pairs),14)

    def test_descriptor_equality_does_not_mask_champion_ast_change(self):
        with tempfile.TemporaryDirectory() as folder:
            folder=Path(folder);jobs={}
            for variant,ast in [('gpu_base',{'node':1}),('gpu_exec',{'node':2})]:
                output=folder/(variant+'.json');export=folder/(variant+'.export.json');history=folder/(variant+'.history.json')
                output.write_text(json.dumps(dict(final_population=[dict(fitness=0,wrong=0,nodes=1)])))
                export.write_text(json.dumps(dict(programs=[ast])))
                history.write_text(json.dumps(dict(history=[dict(ast=ast,fitness=0,elapsed_search_ms=1)])))
                jobs['task.s5.'+variant]=dict(status='ok',output=str(output),export=str(export),history=str(history))
            result=report.execution_identity(dict(tasks=['task'],seeds=[5]),dict(jobs=jobs))[0]
            self.assertTrue(result['final_score_wrong_size_arrays_equal'])
            self.assertTrue(result['all_observed_champion_fitness_equal'])
            self.assertFalse(result['final_champion_ast_equal'])
            self.assertFalse(result['all_observed_champion_asts_equal'])


if __name__=='__main__':unittest.main()
