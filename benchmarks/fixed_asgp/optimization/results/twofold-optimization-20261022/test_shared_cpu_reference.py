import copy
import unittest

import shared_cpu_reference as shared


class TestSharedCpuReference(unittest.TestCase):
    def fixture(self):
        source=dict(task='t',cases=[dict(inputs=[1],expected=2)])
        programs=[dict(nodes=[dict(op='literal',value=1)])]
        args=(programs,source,'grammar',dict(fuel='same'))
        return args

    def test_identity_requires_full_ast_order_cases_labels_grammar_and_policy(self):
        args=self.fixture();self.assertTrue(shared.same_evaluation(*args,*copy.deepcopy(args))[0])
        for index in range(4):
            other=list(copy.deepcopy(args))
            if index==0:other[0][0]['nodes'][0]['value']=2
            elif index==1:other[1]['cases'][0]['expected']=3
            elif index==2:other[2]='other grammar'
            else:other[3]['fuel']='different'
            self.assertFalse(shared.same_evaluation(*args,*other)[0])

    def test_python_numeric_aliases_do_not_prove_exact_serialized_identity(self):
        args=list(self.fixture());other=copy.deepcopy(args)
        args[1]['cases'][0]['expected']=1;other[1]['cases'][0]['expected']=True
        self.assertFalse(shared.same_evaluation(*args,*other)[0])
        args[1]['cases'][0]['expected']=0.;other[1]['cases'][0]['expected']=-0.
        self.assertFalse(shared.same_evaluation(*args,*other)[0])

    def test_full_literal_decode_prefix_is_part_of_reference_eligibility(self):
        programs=[{'ast':'one'}]
        source=dict(task='t',cases=[1],population=[{'literal':[2,3]},{'unused':4}],limit=8)
        reference=copy.deepcopy(source);reference['population']=reference['population'][:1]
        reference['diagnostic_source_parents']='decode-only'
        self.assertTrue(shared.same_decode_context(programs,source,reference))
        reference['population'][0]['literal'][0]=9
        self.assertFalse(shared.same_decode_context(programs,source,reference))
        reference=copy.deepcopy(source);reference['limit']=9
        self.assertFalse(shared.same_decode_context(programs,source,reference))

    def test_gpu_is_fresh_and_only_cpu_fields_are_shared(self):
        gpu=dict(task='t',cases=3,population=1,diagnostic_owner='native-gpu-phase-v2',programs=[dict(
            program=0,nodes=1,cases=3,unscored=0,fitness=-2.,wrong=1,errors=0,timeouts=0)])
        reference=copy.deepcopy(gpu)
        reference['programs'][0].update(cpu_fitness=-2.,cpu_wrong=1,cpu_cases=3,cpu_errors=0,cpu_timeouts=0)
        merged=shared.merge(gpu,reference);expanded=shared.expand(merged,[0,0])
        self.assertNotIn('cpu_fitness',gpu['programs'][0])
        self.assertEqual(expanded['population'],2)
        self.assertEqual(expanded['deduplication']['native_cpu_executed_programs'],0)
        self.assertEqual(expanded['deduplication']['native_cpu_reference_programs'],1)
        with self.assertRaises(AssertionError):shared.merge(reference,reference)
        invalid=copy.deepcopy(gpu);invalid['programs'][0]['program']=1
        with self.assertRaises(AssertionError):shared.merge(invalid,reference)

    def test_zero_cpu_calls_without_verified_reference_are_not_coverage(self):
        row=dict(native_cpu_executed_programs=0,native_cpu_executed_cases=0)
        self.assertFalse(shared.coverage_valid(row,10,3))
        row.update(cpu_reference_verified=True,native_cpu_reference_programs=2,native_cpu_reference_cases=6,
                   cpu_reference={'proof':'checked by reader'},gpu_raw_output_sha256='x'*64)
        self.assertTrue(shared.coverage_valid(row,10,3))
        row['native_cpu_reference_cases']=5;self.assertFalse(shared.coverage_valid(row,10,3))


if __name__=='__main__':unittest.main()
