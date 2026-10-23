from pathlib import Path
from tempfile import TemporaryDirectory
from unittest import TestCase, mock

import quality_dedup


class TestQualityReportingReserve(TestCase):
    def test_common_judge_retains_full_reporting_reserve(self):
        class Stage:
            def run(self,*args,**kwargs):
                self.kwargs=kwargs;return {'status':'budget_not_run'}
            def persist(self):pass
        run=dict(status='ok',task='sum_of_elements',seed=25,variant='candidate',
                 export='/unused/export',history='/unused/history',export_sha256='frozen',history_sha256='frozen',generations=64)
        inputs=dict(grammar='/unused/grammar',test_source='/unused/test',
                    metadata=dict(train_count=200,test_count=1000),seeds={'25':{'source':'/unused/train'}})
        with TemporaryDirectory() as temporary,mock.patch.object(quality_dedup.serial,'sha',return_value='frozen'),mock.patch.object(quality_dedup.fcntl,'flock'):
            stage=Stage();stage.out=Path(temporary)
            result=quality_dedup.judge(stage,run,'test',inputs)
            self.assertEqual(result['status'],'budget_not_run')
            self.assertEqual(stage.kwargs['reserve'],10800)
            quality_dedup.judge(stage,run,'test',inputs,reserve=14400)
            self.assertEqual(stage.kwargs['reserve'],14400)
