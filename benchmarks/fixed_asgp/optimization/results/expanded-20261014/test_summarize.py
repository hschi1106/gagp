"""Synthetic contract checks; no benchmark execution or GPU work."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('expanded_summary', Path(__file__).with_name('summarize.py'))
summary = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(summary)


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value))


def fixture(root):
    cells, quality = [], []
    for task in summary.TASKS:
        save(root/'artifacts'/f'{task}.audit.json', dict(known_solution_cpu_mismatches=0,
            known_solution_asgp_wrong=0, known_solution_gpu_fitness=-1 if task=='last_index_of_zero' else 0))
        for mode in summary.MODES:
            for seed in summary.SEEDS:
                tag = f'{task}.{mode}.{seed}'
                gpu = mode.startswith('gpu')
                rows = [dict(generation=g, generation_ms=10+g, best=-100,
                    expected_program_cases=1024**2, case_counters_available=int(gpu),
                    cases=1024**2, unscored=0, errors=2, timeouts=1, repro_kernel_ms=1) for g in range(4)]
                output = root/'formal'/f'{tag}.json'
                save(output, dict(population=1024, cases=1024, seed=seed, mode=mode,
                    generations=rows, final_population=[{}]*1024, final_best=-100,
                    evolve_wall_ms=(seed+1)*100 if mode=='asgp_1t' else (seed+1)*50,
                    reproduction_profile='generic_gpu' if gpu else 'cpu',
                    cpu_audit_mismatches=0, cpu_audit_program_count=1024, unique_final_genomes=12))
                cells.append(dict(task=task, mode=mode, seed=seed, status='ok', output=str(output), process_wall_s=1))
                score = dict(cases=1024, case_results=[{}]*1024, exact_accuracy=.5, runtime_errors=0, capped_l1_sum=512)
                path = root/'quality'/f'{tag}.json'
                save(path, dict(task=task, cases=[{}]*1024, cases_sha256=task,
                    final=score, history=[dict(score, generation=g) for g in (0,2,3)]))
                quality.append(dict(task=task, mode=mode, seed=seed, status='ok', output=str(path)))
    save(root/'formal/manifest.json', dict(flags={'GAGP_BM_COMMON_FITNESS':'1','GAGP_HOST_THREADS':'1'}, cells=cells, generations={t:4 for t in summary.TASKS}, artifacts=str(root/'artifacts')))
    save(root/'quality/manifest.json', dict(cells=quality))


class TestSummary(unittest.TestCase):
    def test_complete_seed_pairs_and_correctness_gate(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); fixture(root)
            aggregate, records, curves, quality, missing=summary.summarize(root)
            self.assertEqual((len(records),len(quality),missing), (30,120,[]))
            row=next(r for r in aggregate if r['task']=='last_index_of_zero' and r['mode']=='gpu_repro')
            self.assertEqual(row['measured_search_ms_ratio_median'],2)
            self.assertIsNone(row['qualified_search_ms_speedup_median'])
            self.assertIn('GPU_evaluation_failure',row['qualification_reason'])
            self.assertEqual(row['reproduction_profiles'],'generic_gpu')
            self.assertEqual(summary.normal_fitness('negative_to_zero','asgp_1t',-100),924)
            self.assertNotIn('negative_to_zero', summary.TASKS)
            self.assertIn('negative_to_zero', summary.UNMEASURED)
            self.assertIn('stock_with_cooldown', summary.UNMEASURED)
            cpu=next(r for r in aggregate if r['task']=='count_odds' and r['mode']=='gagp_cpu')
            self.assertEqual(cpu['qualified_search_ms_speedup_median'],2)
            self.assertIsNone(cpu['errors_median'])

    def test_unmeasured_task_cannot_enter_formal_ratios(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); fixture(root)
            path=root/'formal/manifest.json'
            data=summary.read(path);data['cells'][0]['task']='negative_to_zero';save(path,data)
            with self.assertRaisesRegex(ValueError,'out-of-scope formal cell'):
                summary.summarize(root)

    def test_missing_quality_requires_explicit_partial(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); fixture(root)
            save(root/'quality/manifest.json', {'cells':[]})
            with self.assertRaisesRegex(ValueError,'Missing cells'):
                summary.summarize(root)
            self.assertEqual(len(summary.summarize(root,True)[-1]),30)

    def test_incomplete_case_audit_and_cpu_fallback_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); fixture(root)
            path=root/'quality/count_odds.gpu_eval.0.json'
            data=summary.read(path);data['final']['case_results']=[];save(path,data)
            with self.assertRaisesRegex(ValueError,'not full-case'):
                summary.summarize(root)
            fixture(root)
            path=root/'formal/count_odds.gpu_repro.0.json'
            data=summary.read(path);data['reproduction_profile']='cpu';save(path,data)
            with self.assertRaisesRegex(ValueError,'GPU reproduction not exercised'):
                summary.summarize(root)


if __name__=='__main__':
    unittest.main()
