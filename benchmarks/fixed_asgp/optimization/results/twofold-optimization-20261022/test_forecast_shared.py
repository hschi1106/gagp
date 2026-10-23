import copy
import unittest
from unittest.mock import patch

import forecast_shared


class TestSharedReferenceForecast(unittest.TestCase):
    def fixture(self):
        rows=[dict(population=1024,process_s=1.,new_cpu_executions=0) for _ in range(4)]
        rows.append(dict(population=16384,process_s=5.,new_candidate_cpu_executions=0))
        smoke=dict(status='ok',rows=rows)
        base=dict(speed_cost_breakdown={g:dict(search_process_s=100*g/8,
            fresh_full_native_cpu_forecast_s=400*g/8) for g in (8,16,32)},
            quality_per_seed=dict(total_s=100.),available_s=100000.,
            final_regression_and_fixed_floor_s=1800.,cost_evidence_sha256={})
        return base,smoke

    def calculate(self,base,smoke):
        with patch.object(forecast_shared.forecast,'calculate',return_value=copy.deepcopy(base)),\
             patch.object(forecast_shared.serial,'read',side_effect=[smoke,dict(tasks=list(range(30)))]),\
             patch.object(forecast_shared.serial,'sha',return_value='a'*64):
            return forecast_shared.calculate('training12','training-timing7','preparation-timing5')

    def test_reference_cpu_once_but_gpu_overhead_both_methods(self):
        base,smoke=self.fixture();result=self.calculate(base,smoke)
        detail=result['speed_cost_breakdown'][8]
        self.assertEqual(detail['fresh_single_paired_cpu_reference_s'],200.)
        self.assertEqual(detail['fresh_native_gpu_construction_forecast_s'],1020.)
        self.assertEqual(result['speed_costs_s'][8],1320.)
        self.assertEqual(result['choice']['quality_seed_count'],80)
        self.assertEqual(result['choice']['speed_generations'],32)

    def test_unadmitted_or_nonzero_candidate_cpu_work_blocks_discount(self):
        base,smoke=self.fixture();smoke['status']='stopped'
        with self.assertRaises(AssertionError):self.calculate(base,smoke)
        base,smoke=self.fixture();smoke['rows'][0]['new_cpu_executions']=1
        with self.assertRaises(AssertionError):self.calculate(base,smoke)


if __name__=='__main__':unittest.main()
