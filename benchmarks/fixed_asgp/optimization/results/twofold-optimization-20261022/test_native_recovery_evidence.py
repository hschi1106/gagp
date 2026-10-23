import copy
import unittest

from native_recovery_evidence import retained_jobs


class TestNativeRecoveryEvidence(unittest.TestCase):
    def fixture(self):
        old={'ok':dict(status='ok',pid=1,native_fidelity_pass=True),
             'lost':dict(status='running',pid=2,command=['pinned'],process_s=20.)}
        new=copy.deepcopy(old)
        new['lost'].update(status='interrupted',native_execution_coverage_unknown=True,
            native_fidelity_pass=False,process_s_is_lower_bound=True)
        new['new']={'status':'ok','started_epoch':101.}
        return old,new

    def test_preserves_success_and_unknown_without_mutation(self):
        old,new=self.fixture();before=copy.deepcopy((old,new))
        result=retained_jobs(old,new,100.)
        self.assertEqual(result['retained_interrupted_tags'],['lost'])
        self.assertEqual(result['newly_recorded_attempts'],1)
        self.assertEqual((old,new),before)

    def test_rejects_deletion_hidden_rerun_and_success_promotion(self):
        old,new=self.fixture()
        cases=[]
        x=copy.deepcopy(new);del x['ok'];cases.append(x)
        for tag,field,value in [('ok','pid',9),('lost','pid',9),('lost','status','ok'),
                                 ('lost','native_fidelity_pass',True),('new','started_epoch',99.)]:
            x=copy.deepcopy(new);x[tag][field]=value;cases.append(x)
        for x in cases:
            with self.assertRaises(ValueError):retained_jobs(old,x,100.)
