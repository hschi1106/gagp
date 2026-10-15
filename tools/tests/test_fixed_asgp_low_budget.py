from __future__ import annotations

import copy
from pathlib import Path
import sys
import unittest
from types import SimpleNamespace
from tempfile import TemporaryDirectory
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from gagp_tools.experiments.fixed_asgp.low_budget import PAPER_TASKS, validate_search, snapshot_mismatches, Study


class TestLowBudgetContract(unittest.TestCase):
    def test_reduced_depth_rejects_profile_without_dropping_parents_or_witness(self):
        for failure in ('admit1024','audit'):
            with self.subTest(failure=failure),TemporaryDirectory() as folder:
                study=Study.__new__(Study)
                study.out=Path(folder)
                study.args=SimpleNamespace(binary=Path('/unused'),native_depth=16)
                study.manifest={'tasks':{}}
                phases=[]
                def run(task,phase,command,extra=None):
                    phases.append((phase,command))
                    return {'status':'failed' if phase==failure else 'ok'}
                study.run=run
                with patch('gagp_tools.experiments.fixed_asgp.low_budget.read',return_value={}), \
                     patch('gagp_tools.experiments.fixed_asgp.low_budget.definition',return_value={'search_limits':{'max_nodes':1024,'max_depth':16}}):
                    study.support('median')
                expected=['contract','freeze','admit1024']
                if failure=='audit':expected+=['prepare','audit']
                self.assertEqual([phase for phase,_ in phases],expected)
                self.assertEqual(phases[2][1][-2],1024)
                self.assertEqual(study.manifest['tasks']['median']['blocked_at'],failure)

    def test_snapshot_requires_status_parity_even_when_fitness_matches(self):
        row = dict(fitness=-7, cpu_fitness=-7, cases=3, cpu_cases=3,
                   errors=2, cpu_errors=2, timeouts=1, cpu_timeouts=1)
        self.assertEqual(snapshot_mismatches([row]), 0)
        for key in ('cases', 'errors', 'timeouts'):
            changed = dict(row, **{'cpu_' + key: row[key] + 1})
            self.assertEqual(snapshot_mismatches([changed]), 1)
            del changed['cpu_' + key]
            self.assertEqual(snapshot_mismatches([changed]), 1)
        self.assertEqual(snapshot_mismatches([dict(row, fitness=float('nan'))]), 1)

    def sample(self):
        return {'population': 1024, 'cases': 1024, 'final_best': -12,
                'final_population': [{}] * 1024, 'cpu_audit_mismatches': 0,
                'reproduction_profile': 'gpu-phase-population',
                'generations': [{'expected_program_cases': 1024**2, 'repro_kernel_ms': 1,
                                 'case_counters_available': 1, 'cases': 1024**2, 'eval_kernel_ms': 1}] * 2}

    def test_exact_paper_scope(self):
        self.assertEqual(len(PAPER_TASKS), 30)
        self.assertEqual(len(set(PAPER_TASKS)), 30)
        self.assertIn('negative_to_zero', PAPER_TASKS)
        self.assertIn('stock_with_cooldown', PAPER_TASKS)
        self.assertNotIn('mirror_image', PAPER_TASKS)

    def test_complete_native_gpu_search(self):
        validate_search(self.sample(), 1024, 2, 'gpu_overlap')

    def test_requested_multigeneration_count_is_enforced(self):
        sample = self.sample()
        sample['generations'] *= 4
        validate_search(sample, 1024, 8, 'gpu_overlap')
        with self.assertRaises(ValueError):
            validate_search(sample, 1024, 2, 'gpu_overlap')

    def test_incomplete_or_wrong_backend_does_not_qualify(self):
        for key, value in [('cases', 128), ('population', 8), ('generations', []),
                           ('final_population', []), ('final_best', float('nan')),
                           ('cpu_audit_mismatches', 1), ('reproduction_profile', 'cpu')]:
            sample = self.sample()
            sample[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_search(sample, 1024, 2, 'gpu_overlap')
        sample = copy.deepcopy(self.sample())
        sample['generations'][0]['expected_program_cases'] -= 1
        with self.assertRaises(ValueError):
            validate_search(sample, 1024, 2, 'gpu_overlap')

    def test_elite_cannot_lose_best_training_fitness(self):
        sample = copy.deepcopy(self.sample())
        sample['generations'][0]['best'] = -1
        with self.assertRaisesRegex(ValueError, 'elite lost'):
            validate_search(sample, 1024, 2, 'gpu_overlap')

    def test_gpu_eval_requires_measured_work_and_kernel(self):
        for key, value in [('cases', 0), ('case_counters_available', 0), ('eval_kernel_ms', 0)]:
            sample = copy.deepcopy(self.sample())
            sample['generations'][0][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_search(sample, 1024, 2, 'gpu_eval')


if __name__ == '__main__':
    unittest.main()
