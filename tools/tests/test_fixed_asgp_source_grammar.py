from __future__ import annotations
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from gagp_tools.experiments.fixed_asgp.grammar import definition
from gagp_tools.experiments.fixed_asgp.source_grammar import PAPER_TASKS
from gagp_tools.experiments.fixed_asgp.dp_grammar import template
ROOT=Path(__file__).resolve().parents[2]

def production(kind,typ,children=(),op=0):
    return dict(kind=kind,type=typ,children=list(children),op=op)

class TestSourceGrammar(unittest.TestCase):
    def test_paper_catalog_uses_source_identifiers(self):
        self.assertEqual(len(set(PAPER_TASKS)),30)
        self.assertIn('min',PAPER_TASKS)
        self.assertIn('mpi',PAPER_TASKS)
        self.assertNotIn('minimum',PAPER_TASKS)

    def test_character_language_does_not_gain_integer_arithmetic(self):
        contract=dict(task='character_fixture',scheme='divide-and-conquer',carrier='charList',
            scalars=['threshold'],base_size=1,ints=[['0','2']],chars=[['32','32'],['10','10']],
            phases=[dict(start='charList',productions=[production(2,'char'),production(1,'int'),
                production(6,'int'),production(16,'charList',['char'])]),
                dict(start='int',productions=[production(1,'int')]),
                dict(start='charList',productions=[production(7,'charList')])])
        document=definition(ROOT,'character_fixture',contract=contract)
        nts={nt['id']:nt for nt in document['nonterminals']}
        singleton=nts['Solve.charList']['alternatives'][0]['expression']
        self.assertEqual(singleton['args'],[{'ref':'Solve.char'}])
        self.assertEqual([p['expression']['constant']['range'] for p in nts['Solve.char']['alternatives']],contract['chars'])
        self.assertEqual(document['templates'][0]['body']['captures'],[{'input':'scalar0'}])
        self.assertIn({'name':'scalar0','type':'Int'},nts['Solve.char']['scope'])
        self.assertNotIn({'name':'scalar0','type':'Int'},nts['Divide.int']['scope'])
        with self.assertRaisesRegex(ValueError,'task mismatch'):
            definition(ROOT,'wrong',contract=contract)

    def test_coordinate_dependencies_preserve_source_order_and_dimension(self):
        contract=dict(dp_dimensions=2,dp_deps=[[-1,0],[0,-1]],sequence_input=0)
        body=template(ROOT,'fibonacci_array',contract)['body']
        plan=body['structured']['plan']
        self.assertEqual([[int(s['offset']) for s in request['states']] for request in plan['requests']],contract['dp_deps'])
        self.assertEqual(plan['coordinate_slots'],[0,1])
        self.assertEqual(plan['limits']['cells'],169)
        self.assertEqual(body['args'][:2],[{'input':'start_a'},{'input':'start_b'}])
        self.assertEqual([p['argument'] for p in body['phases']],[2,3,4,5])
        stock=template(ROOT,'stock_with_cooldown',dict(dp_dimensions=2,dp_deps=[[1,0],[1,1],[2,-1]],sequence_input=1))['body']
        self.assertEqual(stock['structured']['plan']['coordinate_rank'][0]['direction'],-1)
        self.assertTrue(all(r['states'][1]['kind']=='expression' for r in stock['structured']['plan']['requests']))

if __name__=='__main__':
    unittest.main()
