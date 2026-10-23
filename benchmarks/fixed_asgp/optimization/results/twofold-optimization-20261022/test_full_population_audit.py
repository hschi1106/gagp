import copy
import unittest

import champion_audit_dedup
import full_population_audit


class TestFullPopulationFidelity(unittest.TestCase):
    def fixture(self):
        rows=[dict(program=i,cases=3,cpu_cases=3,unscored=0,fitness=f,cpu_fitness=f,
            wrong=w,cpu_wrong=w,errors=0,cpu_errors=0,timeouts=0,cpu_timeouts=0)
            for i,(f,w) in enumerate([(0.,0),(-2.,1)])]
        raw=dict(population=2,cases=3,programs=rows,diagnostic_owner='native-gpu-phase-v2-counted')
        doc=champion_audit_dedup.expand_document(raw,[0,1,1,0])
        search=dict(final_population=[dict(fitness=f,wrong=w) for f,w in [(0.,0),(-2.,1),(-2.,1),(0.,0)]])
        return doc,search

    def test_every_final_member_is_checked_with_actual_execution_counts_separate(self):
        doc,search=self.fixture();result=full_population_audit.validate(doc,search,4,3)
        self.assertTrue(result['native_fidelity_pass']);self.assertEqual(result['native_case_observations'],12)
        self.assertEqual(doc['deduplication']['native_cpu_executed_cases'],6)

    def test_gpu_cpu_and_original_search_scores_have_independent_guards(self):
        original,search=self.fixture()
        for field in ('fitness','wrong','errors','timeouts'):
            doc=copy.deepcopy(original);doc['programs'][1][field]+=1
            self.assertFalse(full_population_audit.validate(doc,search,4,3)['native_fidelity_pass'])
        search['final_population'][3]['fitness']=-1.
        value=full_population_audit.validate(original,search,4,3)
        self.assertEqual(value['native_mismatches']['search_cpu_fitness'],1)

    def test_missing_members_cases_and_wrong_row_order_are_rejected(self):
        original,search=self.fixture()
        for field in ('cases','cpu_cases','unscored','program'):
            doc=copy.deepcopy(original);doc['programs'][2][field]+=1
            with self.assertRaises(AssertionError):full_population_audit.validate(doc,search,4,3)
        doc=copy.deepcopy(original);doc['programs'].pop()
        with self.assertRaises(AssertionError):full_population_audit.validate(doc,search,4,3)


if __name__=='__main__':unittest.main()
