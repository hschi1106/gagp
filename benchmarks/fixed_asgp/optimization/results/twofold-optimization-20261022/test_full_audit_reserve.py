import unittest
from unittest.mock import patch
import full_population_audit_v2 as audit


class Stage:
    def __init__(self):self.m={'jobs':{}};self.persisted=0
    def persist(self):self.persisted+=1


class TestFullNativeReserve(unittest.TestCase):
    def fixture(self,status='ok'):
        return dict(status=status,task='x',population=16384,variant='candidate',seed=0,repetition=0)

    def test_cutoff_preserves_missing_audit_without_opening_large_or_missing_files(self):
        stage=Stage()
        with patch.object(audit.time,'time',return_value=audit.serial.DEADLINE-10800),patch.object(audit.admitted,'run') as body:
            row=audit.run(stage,self.fixture(),{},dict(reserve_s=10800))
        self.assertEqual(row['status'],'budget_not_run');self.assertTrue(row['inputs_not_read'])
        self.assertEqual(stage.persisted,1);body.assert_not_called()

    def test_budgeted_audit_and_search_failure_keep_admitted_body_and_failure_semantics(self):
        stage=Stage()
        for status,now in [('ok',audit.serial.START),('failed',audit.serial.DEADLINE)]:
            job=self.fixture(status)
            with patch.object(audit.time,'time',return_value=now),patch.object(audit.admitted,'run',return_value='unchanged') as body:
                self.assertEqual(audit.run(stage,job,{},dict(reserve_s=10800)),'unchanged')
                body.assert_called_once_with(stage,job,{},dict(reserve_s=10800))


if __name__=='__main__':unittest.main()
