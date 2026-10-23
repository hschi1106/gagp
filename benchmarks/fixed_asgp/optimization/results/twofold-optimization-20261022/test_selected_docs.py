from unittest import TestCase
import align_selected_docs as module

class TestSelectedDocs(TestCase):

    def test_remove_only_owned_missing_feature(self):
        docs = {'d': 'unrelated introduction\nowned feature\nunrelated ending\n'}
        blocks = [dict(flag='OPTION', path='d', text='owned feature\n')]
        (result, removed) = module.rewrite(docs, set(), blocks)
        self.assertEqual(result, {'d': 'unrelated introduction\nunrelated ending\n'})
        self.assertEqual(docs['d'], 'unrelated introduction\nowned feature\nunrelated ending\n')
        self.assertEqual(removed, [('OPTION', 'd')])

    def test_implemented_optional_feature_is_retained_even_if_disabled(self):
        docs = {'d': 'prefix\nowned\nend\n'}
        blocks = [dict(flag='OPTION', path='d', text='owned\n')]
        (result, removed) = module.rewrite(docs, {'OPTION'}, blocks)
        self.assertEqual(result, docs)
        self.assertFalse(removed)

    def test_changed_owned_text_refuses_before_mutation(self):
        docs = {'d': 'prefix\nuser changed feature\nend\n'}
        with self.assertRaisesRegex(ValueError, 'block changed'):
            module.rewrite(docs, set(), [dict(flag='OPTION', path='d', text='owned\n')])
        self.assertIn('user changed', docs['d'])

    def test_adjacent_addition_requires_actual_source_flag(self):
        intro = 'This document explains how C++ runtime container values work under `cpp/` and how the exact and fallback paths interact.\n\n'
        docs = {module.PAYLOAD: '# Title\n\n' + intro + '## Old section\nold text\n', module.NAV: '- Resident payload scoring is shared by the VM unchanged\n'}
        (unchanged, _) = module.rewrite(docs, set(), [])
        self.assertEqual(unchanged, docs)
        (changed, _) = module.rewrite(docs, {module.ADJACENT}, [])
        self.assertIn('## Old section\nold text\n', changed[module.PAYLOAD])
        self.assertIn('64 actual cells and2143', changed[module.PAYLOAD])
        self.assertIn(module.ADJACENT, changed[module.NAV])
