from pathlib import Path
import tempfile
import unittest

import development_evidence
import report_development


class TestDevelopmentReportGuards(unittest.TestCase):
    def fixture(self):
        rows = [dict(family='study', variant='candidate', task='a', population=p,
            status='ok', speedup=3, final_descriptors_equal=True,
            full_final_ast_equal=True, champion_ast_fitness_equal=True,
            candidate_binary_sha256='candidate') for p in (1024, 16384)]
        result = development_evidence.summarize(rows, ['a'])
        result['cells'] = rows
        selection = dict(selected=dict(stage_family='study', variant='candidate',
                                       binary_sha256='candidate'))
        return result, selection

    def test_requires_complete_actual_selected_recipe_and_binary(self):
        result, selection = self.fixture()
        self.assertTrue(report_development.choose_reported(result, selection)['complete'])
        selection['selected']['binary_sha256'] = 'another'
        with self.assertRaisesRegex(ValueError, 'binary'):
            report_development.choose_reported(result, selection)
        result, selection = self.fixture()
        result['recipes'][0]['complete'] = False
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            report_development.choose_reported(result, selection)

    def test_real_formal_freeze_cannot_be_replaced_with_development_partial(self):
        with tempfile.TemporaryDirectory() as directory:
            raw = Path(directory)
            report_development.guard_kind('partial', raw)
            (raw / 'formal0-speed-freeze.json').write_text('{}')
            with self.assertRaisesRegex(ValueError, 'actual formal freeze'):
                report_development.guard_kind('partial', raw)
            report_development.guard_kind('appendix', raw)

    def test_report_requires_explicit_terminal_controller_list(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                report_development.terminal(Path(directory), [])


if __name__ == '__main__':
    unittest.main()
