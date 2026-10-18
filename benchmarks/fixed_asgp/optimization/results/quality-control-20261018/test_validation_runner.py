"""Guard formal work/admission requirements without running an experiment."""
import copy
import unittest

import run_validation


class TestValidationRunner(unittest.TestCase):
    def fixture(self):
        return dict(population=2,cases=3,generations=[dict(expected_program_cases=6,
            cases=6,unscored=0,eval_kernel_ms=1,repro_kernel_ms=1)],
            final_population=[dict(nodes=10)]*2,final_best=0,evolve_wall_ms=10,
            reproduction_profile='native-gpu-phase-v4',reproduction_fallback_reason='',
            cpu_audit_program_count=16,cpu_audit_wrong_program_count=16,
            cpu_audit_mismatches=1,cpu_audit_wrong_mismatches=1)

    def test_work_complete_does_not_discard_fidelity_observation(self):
        # A mismatch must reach the report, not silently become a missing run.
        run_validation.validate_run(self.fixture(),'gpu_overlap',2,3,1)

    def test_rejects_cpu_reproduction_and_unscored(self):
        for field,value in [('reproduction_profile','native-cpu'),('reproduction_fallback_reason','cpu')]:
            row=self.fixture();row[field]=value
            with self.assertRaises(AssertionError):run_validation.validate_run(row,'gpu_overlap',2,3,1)
        row=self.fixture();row['generations'][0]['unscored']=1
        with self.assertRaises(AssertionError):run_validation.validate_run(row,'gpu_overlap',2,3,1)

    def test_requires_both_fitness_and_wrong_audits(self):
        for field in ('cpu_audit_program_count','cpu_audit_wrong_program_count'):
            row=self.fixture();row[field]=0
            with self.assertRaises(AssertionError):run_validation.validate_run(row,'gpu_overlap',2,3,1)

    def test_rejects_incomplete_work(self):
        row=self.fixture();row['generations'][0]['expected_program_cases']=3
        with self.assertRaises(AssertionError):run_validation.validate_run(row,'gpu_overlap',2,3,1)


if __name__=='__main__':unittest.main()
