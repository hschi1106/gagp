import copy
import unittest
from gagp_tools.experiments.fixed_asgp.source_depth import annotate


def fixture(expr):
    return {'entry': {'nonterminal': 'Root'}, 'templates': [
        {'body': {'structured': {'plan': {}}, 'args': [{'hole': 'solve'}]}}],
        'nonterminals': [
            {'id': 'Root', 'alternatives': [{'expression': {'template': 'T'}}]},
            {'id': 'Solve.Bool', 'alternatives': [{'expression': expr}]}]}


class TestSourceDepth(unittest.TestCase):
    def test_lowering_counts_one_source_production(self):
        original = fixture({'signature': 'index(IntList,Int)->Int',
            'args': [{'bound': 'xs'}, {'signature': 'max(Int,Int)->Int',
                'args': [{'ref': 'Solve.Int'}, {'constant': {'type': 'Int', 'values': ['0']}}]}]})
        saved = copy.deepcopy(original)
        got = annotate(original)
        expr = got['nonterminals'][1]['alternatives'][0]['expression']
        self.assertEqual(original, saved)
        self.assertEqual(expr['resource_charge']['depth'], 1)
        self.assertEqual(expr['args'][0]['resource_charge']['depth'], 0)
        self.assertEqual(expr['args'][1]['resource_charge']['depth'], 0)
        self.assertEqual(expr['args'][1]['args'][0], {'ref': 'Solve.Int'})
        body = got['templates'][0]['body']
        self.assertTrue(body['resource_charge']['resets_depth'])
        self.assertEqual(body['resource_charge']['depth'], 0)
        self.assertEqual(body['args'], [{'hole': 'solve'}])

    def test_boolean_neutral_leaf_preserves_source_height(self):
        for left in (True, False):
            ref = {'ref': 'Solve.Bool'}
            lit = {'constant': {'type': 'Bool', 'values': [left]}}
            args = [ref, lit, ref] if left else [ref, ref, lit]
            expr = annotate(fixture({'signature': 'if(Bool,Bool,Bool)->Bool',
                'args': args}))['nonterminals'][1]['alternatives'][0]['expression']
            constant = next(x for x in expr['args'] if 'constant' in x)
            self.assertEqual(constant['resource_charge']['depth'], 1)
            for a in range(1, 8):
                for b in range(1, 8):
                    self.assertEqual(1 + max(a, b, 1), 1 + max(a, b))

    def test_rejects_precharged_or_nonmaterialized_source_roots(self):
        for expr in ({'ref': 'Solve.Int'}, {'template': 'T'},
                     {'bound': 'n', 'resource_charge': {'depth': 3}}):
            with self.subTest(expr=expr), self.assertRaises(ValueError):
                annotate(fixture(expr))
