import copy
import unittest

import champion_audit
import champion_audit as dedup
import test_champion_audit


class TestExactChampionDedup(unittest.TestCase):
    def test_complete_ast_identity_and_mapping(self):
        a={'nodes':[{'id':1,'op':'ADD'}],'consts':[{'tag':'Int','value':'1'}]}
        b={'consts':[{'value':'1','tag':'Int'}],'nodes':[{'op':'ADD','id':1}]}
        c=copy.deepcopy(a);c['nodes'][0]['op']='SUB'
        unique,mapping,hashes=dedup.compress_programs([a,b,c,a])
        self.assertEqual(mapping,[0,0,1,0]);self.assertEqual(len(unique),2)
        self.assertEqual(len(set(hashes)),2)

    def test_numeric_and_constant_flags_are_not_conflated(self):
        values=[{'consts':[x]} for x in [0.,-0.,False,0,1.,1,True]]
        unique,mapping,_=dedup.compress_programs(values)
        self.assertEqual(len(unique),7);self.assertEqual(mapping,list(range(7)))

    def test_empty_and_nonfinite_are_rejected(self):
        for values in ([],[{'consts':[float('nan')]}]):
            with self.assertRaises(ValueError):dedup.compress_programs(values)

    def fixture(self):
        original,search=test_champion_audit.TestNativeChampionAudit().fixture()
        raw=copy.deepcopy(original);raw['population']=2;raw['programs']=raw['programs'][:2]
        return raw,search

    def test_native_audits_expand_every_generation_and_final(self):
        raw,search=self.fixture();expanded=dedup.expand_document(raw,[0,1,1])
        self.assertTrue(champion_audit.validate(expanded,search,'train',3)['native_fidelity_pass'])
        counts=expanded['deduplication']
        self.assertEqual(counts['native_cpu_executed_programs'],2)
        self.assertEqual(counts['native_cpu_executed_cases'],6)
        self.assertEqual(counts['logical_case_observations'],9)
        self.assertEqual(raw['population'],2)
        self.assertEqual([x['program'] for x in expanded['programs']],[0,1,2])

    def test_one_bad_unique_score_cannot_disappear_in_expansion(self):
        raw,search=self.fixture();raw['programs'][1]['cpu_wrong']=7
        result=champion_audit.validate(dedup.expand_document(raw,[0,1,1]),search,'train',3)
        self.assertFalse(result['native_fidelity_pass'])
        self.assertEqual(result['native_mismatches']['cpu_gpu_wrong'],2)
        self.assertEqual(result['native_mismatches']['search_cpu_wrong'],2)

    def test_malformed_mapping_and_coverage_fail_closed(self):
        raw,_=self.fixture()
        for mapping in ([],[0],[0,2],[0,-1],[0,True]):
            with self.assertRaises(AssertionError):dedup.expand_document(raw,mapping)
        for field in ('cases','cpu_cases','unscored','program'):
            broken=copy.deepcopy(raw);broken['programs'][1][field]+=1
            with self.assertRaises(AssertionError):dedup.expand_document(broken,[0,1,1])


if __name__=='__main__':unittest.main()
