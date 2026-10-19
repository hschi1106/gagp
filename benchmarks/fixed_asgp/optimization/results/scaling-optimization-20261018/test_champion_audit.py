import json
from pathlib import Path
import tempfile
from unittest import TestCase, main
from champion_audit import first_native_program, validate_judgment


def judgment(wrong=(1, 1), final_wrong=0, backend='gagp_cpu_1t'):
    def score(n):
        rows = [dict(case=i, exact=int(i >= n), runtime_error=0, common_loss=int(i < n)) for i in range(2)]
        return dict(cases=2, wrong=n, runtime_errors=0, case_results=rows, exact_loss_sum=n,
                    backend=backend, grammar_admitted=1)
    return dict(format='fixed-quality-source-v1', task='fixture', cases=2,
                cases_sha256='a'*64, unique_audited_programs=3,
                final=score(final_wrong), history=[dict(score(n), generation=i) for i,n in enumerate(wrong)])


class TestChampionAudit(TestCase):
    def test_streams_only_preselected_first_program_across_chunk_boundaries(self):
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder)/'export.json'
            program = dict(nodes=[dict(value='a \\" programs value')], payload={'Int':'9223372036854775807'})
            p.write_text(json.dumps(dict(programs=[program, dict(nodes=['different'])])))
            self.assertEqual(first_native_program(p, chunk_size=7), program)

    def test_rejects_empty_truncated_or_different_top_level_export(self):
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder)/'export.json'
            for text in ('{"programs":[]}', '{"programs":[{"nodes":[1]}',
                         '{"other":{"programs":[{}]}}'):
                p.write_text(text)
                with self.assertRaises(ValueError):
                    first_native_program(p, chunk_size=5)

    def test_gen0_success_is_separate_from_success_after_last_reproduction(self):
        initial = validate_judgment(judgment(wrong=(0,0)), 'fixture', 2, 2, 'gpu_improved')
        self.assertTrue(initial['initial_success'])
        self.assertFalse(initial['final_success_after_gen0'])
        self.assertEqual(initial['first_success_generation'], 0)
        later = validate_judgment(judgment(), 'fixture', 2, 2, 'gpu_improved')
        self.assertFalse(later['initial_success'])
        self.assertTrue(later['final_success_after_gen0'])
        self.assertEqual(later['first_success_generation'], 2)

    def test_preserves_lost_initial_success(self):
        result = validate_judgment(judgment(wrong=(0,1),final_wrong=1), 'fixture', 2, 2, 'gpu_original')
        self.assertTrue(result['initial_success_lost'])
        self.assertFalse(result['final_success'])

    def test_missing_generation_or_case_counts_are_not_a_complete_audit(self):
        for field in ('history', 'cases'):
            data = judgment()
            if field == 'history':
                data['history'].pop()
            else:
                data['final']['case_results'].pop()
            with self.assertRaises(AssertionError):
                validate_judgment(data, 'fixture', 2, 2, 'gpu_original')

    def test_wrong_or_native_admission_cannot_disagree_with_cases(self):
        for field, value in (('wrong',0), ('grammar_admitted',0)):
            data = judgment()
            data['history'][0][field] = value
            with self.assertRaises(AssertionError):
                validate_judgment(data, 'fixture', 2, 2, 'gpu_original')

    def test_asgp_audit_uses_source_cpu_backend(self):
        result = validate_judgment(judgment(backend='asgp_1t'), 'fixture', 2, 2, 'asgp')
        self.assertEqual(result['backend'], 'asgp_1t')


if __name__ == '__main__':
    main()
