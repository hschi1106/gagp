"""The new mechanism description must not modify numeric/statistical reporting."""
import ast
from pathlib import Path
from unittest import TestCase
import report_views_implementation as views

class TestImplementationReport(TestCase):

    def test_nonrecipe_views_unchanged(self):
        root = Path(__file__).resolve().parent

        def functions(path):
            source = path.read_text()
            return {n.name: ast.get_source_segment(source, n) for n in ast.parse(source).body if isinstance(n, ast.FunctionDef)}
        before = functions(root / 'report_views_mobile.py')
        after = functions(root / 'report_views_implementation.py')
        self.assertEqual(set(before), set(after))
        for name in before:
            if name != 'recipe_rows':
                self.assertEqual(before[name], after[name], name)

    def test_analysis_entry_unchanged(self):
        root = Path(__file__).resolve().parent
        self.assertEqual((root / 'report_mobile.py').read_text(), (root / 'report_implementation.py').read_text().replace('report_views_implementation', 'report_views_mobile'))

    def test_disabled_mechanism_is_not_advertised(self):
        freeze = {'settings': {}, 'variants': {'candidate': {'extra': {}}}}
        self.assertFalse(any(('相鄰' in title for (title, _) in views.recipe_rows(freeze))))

    def test_enabled_mechanism_states_budget_preservation(self):
        freeze = {'settings': {}, 'variants': {'candidate': {'extra': {'GAGP_GPU_PRIVATE_ADJACENT_CONCAT': '1'}}}}
        rows = [desc for (title, desc) in views.recipe_rows(freeze) if '相鄰' in title]
        self.assertEqual(len(rows), 1)
        self.assertIn('容量上限', rows[0])
        self.assertIn('配置額度', rows[0])
