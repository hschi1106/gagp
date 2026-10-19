from pathlib import Path
import tempfile
from unittest import TestCase, main
from report_audit import FileHashes, compare_execution, expected_job, validate_run
import serial


def native_observation():
    generations, population = 2, 16
    verification = dict(programs=0,cases=0,fitness_corrections=0,wrong_corrections=0,wall_ms=0)
    rows = [dict(generation=i, best=-1, verification=dict(verification), expected_program_cases=population*1024,
                 cases=population*1024, unscored=0, eval_kernel_ms=1, repro_kernel_ms=1,
                 payload_workspace_retained_bytes=1024) for i in range(generations)]
    raw = dict(mode='gpu_overlap', population=population, cases=1024, generations=rows,
        final_population=[dict(nodes=1, fitness=-1, wrong=1) for _ in range(population)],
        evolve_wall_ms=10, reproduction_profile='native-gpu-phase-v2-counted', reproduction_fallback_reason='',
        source_depth_cap=0, gpu_phase_site_depth=0, verification_top_k=0, gpu_phase_site_parsimony=False,
        cpu_audit_program_count=16, cpu_audit_wrong_program_count=16,
        cpu_audit_mismatches=0, cpu_audit_wrong_mismatches=0, gpu_blocksize=512, cpu_audit_reduction_lanes=512,
        cpu_audit_ms=5, verification=dict(verification), verification_interval=1, verification_near_zero=-1)
    job = dict(status='ok', task='fixture', population=16, repetition=0, variant='gpu_improved',
        mode='gpu_overlap', search_s=.01, process_s=.02, full_final_cpu_audit=True,
        cpu_audit_program_count=16, cpu_audit_fitness_mismatches=0, cpu_audit_wrong_mismatches=0,
        fidelity_ok=True, output='out.json', export='export.json', history='history.json', export_sha256='a'*64)
    history = dict(history=[dict(generation=i, ast=dict(nodes=[i])) for i in range(generations)])
    return job, raw, history


class TestReportContractAudit(TestCase):
    def run_audit(self, job, raw, history):
        return validate_run(job, raw, history, 'tag', 'fixture', 16, 0, 'gpu_improved', 2)

    def test_process_completion_with_scoring_mismatch_is_not_fidelity(self):
        job, raw, history = native_observation()
        raw['cpu_audit_mismatches'] = 1
        job.update(cpu_audit_fitness_mismatches=1, fidelity_ok=False)
        record, _ = self.run_audit(job, raw, history)
        self.assertEqual(record['status'], 'ok')
        self.assertFalse(record['fidelity_ok'])
        self.assertEqual(record['cpu_audit_s'], .005)

    def test_unscored_or_cpu_reproduction_fallback_rejects_work_contract(self):
        for problem in ('unscored','fallback'):
            job, raw, history = native_observation()
            if problem == 'unscored':
                raw['generations'][0]['unscored'] = 1
            else:
                raw['reproduction_fallback_reason'] = 'CPU reproduction'
            with self.assertRaises(AssertionError):
                self.run_audit(job, raw, history)

    def test_rejects_incomplete_wrong_count_and_workspace_overcap(self):
        job, raw, history = native_observation()
        raw['cpu_audit_wrong_program_count'] = 1
        with self.assertRaises(AssertionError):
            self.run_audit(job, raw, history)
        job, raw, history = native_observation()
        raw['generations'][0]['payload_workspace_retained_bytes'] = 17*1024**3
        with self.assertRaises(AssertionError):
            self.run_audit(job, raw, history)

    def test_rejects_timed_cpu_guard_separately_from_offline_final_audit(self):
        for location in ('top_level','generation','configuration'):
            job, raw, history = native_observation()
            if location == 'top_level':
                raw['verification']['wall_ms'] = 1
            elif location == 'generation':
                raw['generations'][0]['verification']['programs'] = 1
            else:
                raw['verification_near_zero'] = 0
            with self.assertRaises(AssertionError):
                self.run_audit(job, raw, history)

    def test_missing_generation_champion_rejects_complete_audit(self):
        job, raw, history = native_observation()
        history['history'].pop()
        with self.assertRaises(AssertionError):
            self.run_audit(job, raw, history)

    def test_full_ast_hash_disagreement_is_visible(self):
        job, raw, history = native_observation()
        _, a = self.run_audit(job, raw, history)
        b = dict(a, final_export='different')
        comparison = compare_execution(a, b, 'fixture', 16, 0)
        self.assertTrue(comparison['final_descriptors_equal'])
        self.assertFalse(comparison['final_export_equal'])
        self.assertTrue(comparison['full_final_ast_comparison'])

    def test_expected_command_freezes_tuple_flag_geometry_cases_and_full_cpu(self):
        frozen = dict(generations=8, inputs={'fixture': dict(source='/source', grammar='/grammar',
            prefixes={'16384':dict(prepared='/prepared')})})
        result = expected_job(frozen, '/matrix', 'fixture.p16384', 'fixture', 16384, 0, 'gpu_improved')
        self.assertEqual(result['extra']['GAGP_GPU_PRIVATE_TUPLE_SCORE'], '1')
        self.assertEqual(result['extra']['GAGP_BM_GPU_BLOCKSIZE'], '512')
        self.assertEqual(result['extra']['GAGP_BM_CASE_COUNT'], '1024')
        self.assertEqual(result['extra']['GAGP_SEARCH_CPU_ALL'], '1')
        self.assertNotIn('GAGP_VERIFY_TOP_K', result['extra'])
        self.assertEqual(result['command'][0], '/matrix/bench')

    def test_hash_cache_rejects_mutation_after_first_verification(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'artifact'
            path.write_text('first')
            cache = FileHashes()
            cache.verify(path, serial.sha(path))
            path.write_text('second')
            with self.assertRaises(AssertionError):
                cache.verify_stable()
            with self.assertRaises(AssertionError):
                cache.verify(path, serial.sha(path))


if __name__ == '__main__':
    main()
