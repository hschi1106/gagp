import copy
import unittest

import formal
import quality


class TestNativeOutputContract(unittest.TestCase):
    def valid(self,count=1024,g=8):
        return dict(population=1024,cases=count,generations=[dict(cases=1024*count,
            expected_program_cases=1024*count,unscored=0,repro_kernel_ms=1.) for _ in range(g)],
            final_population=[dict(nodes=4)]*1024,reproduction_profile='native-gpu-phase-v2-counted',
            reproduction_fallback_reason='',verification_top_k=0,source_depth_cap=0,
            gpu_phase_site_depth=0,gpu_phase_site_parsimony=0,gpu_blocksize=512,
            cpu_audit_reduction_lanes=512,cpu_audit_program_count=16,cpu_audit_wrong_program_count=16,
            cpu_audit_mismatches=0,cpu_audit_wrong_mismatches=0,evolve_wall_ms=100.)

    def test_incomplete_cases_and_cpu_mismatch_rejected(self):
        original=self.valid()
        self.assertEqual(formal.validate(original,1024,8,False)['search_s'],.1)
        for path,value in [('cpu_audit_wrong_mismatches',1),('cpu_audit_mismatches',1),
                           ('reproduction_fallback_reason','CPU'),('gpu_phase_site_depth',7)]:
            bad=copy.deepcopy(original);bad[path]=value
            with self.assertRaises(AssertionError):formal.validate(bad,1024,8,False)
        bad=copy.deepcopy(original);bad['generations'][0]['cases']-=1
        with self.assertRaises(AssertionError):formal.validate(bad,1024,8,False)

    def test_full_audit_cannot_be_replaced_by_top16(self):
        output=self.valid()
        with self.assertRaises(AssertionError):formal.validate(output,1024,8,True)
        output['cpu_audit_program_count']=output['cpu_audit_wrong_program_count']=1024
        formal.validate(output,1024,8,True)

    def test_quality_population_and_fuel_work_are_complete(self):
        output=self.valid(count=200,g=64)
        quality.validate(output,200,64)
        bad=copy.deepcopy(output);bad['generations'][31]['unscored']=1
        with self.assertRaises(AssertionError):quality.validate(bad,200,64)
        bad=copy.deepcopy(output);bad['generations'][31]['repro_kernel_ms']=0
        with self.assertRaises(AssertionError):quality.validate(bad,200,64)

    def test_judge_must_cover_each_history_case(self):
        score=dict(backend='gagp_cpu_1t',cases=2,correct_count=1,exact=1,wrong=1,
            runtime_errors=0,exact_accuracy=.5,case_results=[
                dict(case=0,exact=1,runtime_error=0),dict(case=1,exact=0,runtime_error=0)])
        document=dict(task='a',cases=2,cases_sha256='a'*64,history=[copy.deepcopy(score)],final=score)
        quality.validate_judge(document,2,1,'a')
        bad=copy.deepcopy(document);bad['history'][0]['case_results'][1]['case']=0
        with self.assertRaises(AssertionError):quality.validate_judge(bad,2,1,'a')
        bad=copy.deepcopy(document);bad['history'][0]['wrong']=0
        with self.assertRaises(AssertionError):quality.validate_judge(bad,2,1,'a')


if __name__=='__main__':unittest.main()
