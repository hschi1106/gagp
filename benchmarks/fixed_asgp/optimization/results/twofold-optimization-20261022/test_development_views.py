import copy
import unittest

import development_evidence
import development_views


class TestDevelopmentViews(unittest.TestCase):
    def fixture(self):
        rows = [dict(family='study', variant='candidate', task='task', population=p,
            status='ok', speedup=4, final_descriptors_equal=True,
            full_final_ast_equal=True, champion_ast_fitness_equal=True)
            for p in (1024, 16384)]
        result = development_evidence.summarize(rows, ['task'])
        result['cells'] = rows
        return result

    def test_fast_development_mean_does_not_turn_into_formal_or_quality_claim(self):
        text = development_views.markdown(self.fixture())
        self.assertIn('4.000', text)
        self.assertIn('2× 目標尚未由本頁驗證', text)
        self.assertIn('舊 TRAINING', text)
        self.assertIn('CPU16', text)

    def test_no_complete_recipe_is_explicit(self):
        result = development_evidence.summarize([], ['task'])
        result['cells'] = []
        self.assertIn('沒有完整開發組合', development_views.markdown(result))

    def test_selected_partial_recipe_is_rejected(self):
        result = self.fixture()
        result['reported_recipe'] = copy.deepcopy(result['best_complete'])
        result['reported_recipe']['complete'] = False
        with self.assertRaises(ValueError):
            development_views.markdown(result)

    def test_dashboard_exposes_only_development_controls_and_scope(self):
        page = development_views.dashboard(self.fixture(), ['task'])
        for control in ('filter', 'task', 'pop', 'recipe'):
            self.assertIn('id="' + control + '"', page)
        self.assertIn('品質數據不在本頁', page)
        self.assertIn('2× 目標尚未由本頁驗證', page)

    def test_payload_cannot_end_the_inline_script(self):
        result = self.fixture()
        result['cells'][0]['task'] = '</script><script>invalid()</script>'
        page = development_views.dashboard(result, ['</script>'])
        self.assertNotIn('</script><script>invalid()', page)
        self.assertIn(r'\u003c/script>', page)


if __name__ == '__main__':
    unittest.main()
