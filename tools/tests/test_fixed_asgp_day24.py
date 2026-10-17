"""The day24 fixture split must measure held-out inputs rather than resampling."""
import importlib.util
import copy
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
PATH = ROOT / "benchmarks/fixed_asgp/optimization/results/day24-20261017/quality_data.py"
spec = importlib.util.spec_from_file_location("day24_quality_data", PATH)
data = importlib.util.module_from_spec(spec)
spec.loader.exec_module(data)
sys.path.insert(0, str(PATH.parent))
run_spec=importlib.util.spec_from_file_location("day24_run", PATH.with_name("run.py"))
runner=importlib.util.module_from_spec(run_spec)
run_spec.loader.exec_module(runner)


def row(value):
    return {"input": [str(value)], "expected": str(value), "scalars": [],
            "start": 0, "extent": 0, "summary1": "0", "summary2": "0"}


class TestDay24QualitySplit(unittest.TestCase):
    def test_finite_pool_reserves_unseen_inputs_deterministically(self):
        pool = dict(task="finite", data_seed=20261017,
                    train=[row(i) for i in range(10)] * 3,
                    test=[row(i) for i in range(10)] * 2)
        result = data.split_pool(pool)
        self.assertEqual(result, data.split_pool(pool))
        self.assertEqual((len(result["train"]), len(result["test"])), (8, 2))
        self.assertFalse({data.identity(r) for r in result["train"]} &
                         {data.identity(r) for r in result["test"]})
        self.assertEqual(result["metadata"]["reserved_test_inputs"], 2)

    def test_native_extrapolation_support_is_retained(self):
        pool = dict(task="fibonacci", data_seed=20261017,
                    train=[row(i) for i in range(21)], test=[row(i) for i in range(31)])
        result = data.split_pool(pool)
        self.assertEqual((len(result["train"]), len(result["test"])), (21, 10))
        self.assertEqual(result["metadata"]["heldout_outside_training_support"], 10)

    def test_large_domain_is_capped_and_all_oracles_are_consistent(self):
        pool = dict(task="large", data_seed=20261017,
                    train=[row(i) for i in range(4096)], test=[row(i) for i in range(4096)])
        result = data.split_pool(pool)
        self.assertEqual((len(result["train"]), len(result["test"])), (200, 1000))
        pool["test"][0]["expected"] = "bad"
        with self.assertRaises(ValueError):
            data.split_pool(pool)

    def test_singleton_domain_and_zero_limits_are_rejected(self):
        pool = dict(task="one", data_seed=1, train=[row(0)], test=[row(0)])
        with self.assertRaises(ValueError):
            data.split_pool(pool)
        with self.assertRaises(ValueError):
            data.split_pool(pool, train_limit=0)


class TestDay24ExecutionContract(unittest.TestCase):
    def test_quality_case_count_and_missing_gpu_work_are_rejected(self):
        record=dict(population=1024,cases=200,final_best=0,evolve_wall_ms=1,
            final_population=[dict(nodes=3)]*1024,
            reproduction_profile='native-gpu-phase-v2-counted',reproduction_fallback_reason='',
            cpu_audit_program_count=16,cpu_audit_mismatches=0,
            generations=[dict(best=0,expected_program_cases=204800,cases=204800,
                case_counters_available=1,unscored=0,eval_kernel_ms=1,repro_kernel_ms=1)])
        runner.validate(record,1024,1,'gpu_overlap',200)
        for field,value in [('cases',1024),('cpu_audit_mismatches',1)]:
            broken=copy.deepcopy(record);broken[field]=value
            with self.assertRaises(AssertionError):
                runner.validate(broken,1024,1,'gpu_overlap',200)
        broken=copy.deepcopy(record);broken['generations'][0]['unscored']=1
        with self.assertRaises(AssertionError):
            runner.validate(broken,1024,1,'gpu_overlap',200)

    def test_expired_budget_never_launches_and_completed_job_is_not_repeated(self):
        with tempfile.TemporaryDirectory() as directory:
            experiment=runner.Experiment.__new__(runner.Experiment)
            experiment.out=Path(directory)
            experiment.m=dict(started_epoch=time.time()-100,deadline_epoch=time.time()-1,
                              jobs={'finished':dict(status='ok',process_s=4)})
            with patch.object(runner.subprocess,'Popen') as spawn:
                skipped=experiment.run('late',['unused'],reserve=0)
                self.assertEqual(skipped['status'],'budget_not_run')
                self.assertEqual(experiment.run('finished',['unused'])['process_s'],4)
                spawn.assert_not_called()
