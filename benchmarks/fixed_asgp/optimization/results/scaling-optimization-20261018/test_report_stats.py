from unittest import TestCase, main
from report_stats import summarize_cell, population_summary


def observations():
    jobs = []
    for rep, values in enumerate(((12, 6, 3), (24, 16, 8))):
        for variant, seconds in zip(('asgp', 'gpu_original', 'gpu_improved'), values):
            jobs.append(dict(task='fixture', population=1024, variant=variant, repetition=rep,
                search_s=seconds, status='ok', fidelity_ok=True, full_final_cpu_audit=rep == 0,
                cpu_audit_program_count=1024 if rep == 0 else 16))
    comparisons = {f'fixture.p1024.r{rep}': dict(final_descriptors_equal=True,
        champion_fitness_equal=True, champion_ast_equal=True, final_export_equal=True,
        full_final_ast_comparison=rep == 0) for rep in (0, 1)}
    return jobs, comparisons


class TestPairedTimings(TestCase):
    def test_displayed_seconds_reproduce_ratio_and_range_uses_pairs(self):
        cell = summarize_cell(*observations(), 'fixture', 1024, 2)
        self.assertTrue(cell['qualified'])
        self.assertEqual(cell['asgp_s'], 18)
        self.assertEqual(cell['gpu_improved_s'], 5.5)
        self.assertEqual(cell['asgp_vs_improved'], 18/5.5)
        self.assertEqual(cell['asgp_vs_improved_paired_min'], 3)
        self.assertEqual(cell['asgp_vs_improved_paired_max'], 4)

    def test_failed_or_missing_requested_repeat_stays_incomplete(self):
        for missing in (False, True):
            jobs, comparisons = observations()
            if missing:
                jobs.pop()
            else:
                jobs[-1]['status'] = 'budget_not_run'
            cell = summarize_cell(jobs, comparisons, 'fixture', 1024, 2)
            self.assertFalse(cell['qualified'])
            self.assertEqual(cell['valid_pairs'], 1)
            self.assertIsNone(cell['asgp_vs_improved'])
            self.assertEqual(sum(cell['status_counts'].values()), 6)

    def test_process_completion_cannot_replace_cpu_fidelity_or_full_ast(self):
        for invalid in ('fidelity', 'full_cpu', 'full_ast'):
            jobs, comparisons = observations()
            if invalid == 'fidelity':
                jobs[2]['fidelity_ok'] = False
            elif invalid == 'full_cpu':
                jobs[2]['cpu_audit_program_count'] = 16
            else:
                comparisons['fixture.p1024.r0']['full_final_ast_comparison'] = False
            cell = summarize_cell(jobs, comparisons, 'fixture', 1024, 2)
            self.assertEqual(cell['completed_runs'], 6)
            self.assertFalse(cell['qualified'])

    def test_summary_denominator_keeps_incomplete_cells(self):
        cell = summarize_cell(*observations(), 'fixture', 1024, 2)
        partial = dict(cell, task='missing', qualified=False, asgp_vs_improved=None)
        summary = population_summary([cell, partial], 1024)
        self.assertEqual(summary['requested_cells'], 2)
        self.assertEqual(summary['qualified_cells'], 1)
        self.assertAlmostEqual(summary['asgp_vs_improved_geomean'], cell['asgp_vs_improved'])


if __name__ == '__main__':
    main()
