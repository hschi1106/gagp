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
    def test_protected_arithmetic_preserves_one_node_and_operands(self):
        from gagp_tools.experiments.fixed_asgp.arithmetic import protect
        for original, protected in [('idiv0', 'trunc_div0'), ('imod0', 'trunc_mod0')]:
            operands = [{'ref': 'Left'}, {'ref': 'Right'}]
            expr = {'signature': original+'(Int,Int)->Int', 'args': operands}
            document = {'nonterminals': [{'alternatives': [{'expression': expr}]}]}
            protect(document)
            self.assertEqual(expr, {'signature': protected+'(Int,Int)->Int', 'args': operands})
            self.assertIs(expr['args'], operands)

    def test_source_index_is_one_nonnegative_operation(self):
        contract=dict(task='index_fixture',scheme='divide-and-conquer',carrier='int',
            scalars=[],base_size=1,ints=[['0','2']],chars=[],
            phases=[dict(start='int',productions=[production(1,'int'),production(15,'int',['int'])]),
                dict(start='int',productions=[production(1,'int')]),
                dict(start='int',productions=[production(7,'int')])])
        document=definition(ROOT,'index_fixture',contract=contract)
        solve=next(nt for nt in document['nonterminals'] if nt['id']=='Solve.int')
        expr=solve['alternatives'][1]['expression']
        self.assertEqual(expr,{'signature':'index_nonnegative(IntList,Int)->Int',
            'args':[{'bound':'xs'},{'ref':'Solve.int'}]})
        self.assertEqual(len(document['templates']),1)

    def test_paper_catalog_uses_source_identifiers(self):
        self.assertEqual(len(set(PAPER_TASKS)),30)
        self.assertIn('min',PAPER_TASKS)
        self.assertIn('mpi',PAPER_TASKS)
        self.assertNotIn('minimum',PAPER_TASKS)

    def test_character_language_does_not_gain_integer_arithmetic(self):
        contract=dict(task='character_fixture',scheme='divide-and-conquer',carrier='charList',
            scalars=['threshold'],base_size=1,ints=[['0','2']],chars=[['32','32'],['10','10']],
            phases=[dict(start='charList',productions=[production(2,'char'),production(1,'int'),
                production(6,'int'),production(12,'bool',['char']),production(16,'charList',['char'])]),
                dict(start='int',productions=[production(1,'int')]),
                dict(start='charList',productions=[production(7,'charList')])])
        document=definition(ROOT,'character_fixture',contract=contract)
        nts={nt['id']:nt for nt in document['nonterminals']}
        singleton=nts['Solve.charList']['alternatives'][0]['expression']
        self.assertEqual(singleton['args'],[{'ref':'Solve.char'}])
        letter=nts['Solve.bool']['alternatives'][0]['expression']
        self.assertEqual(letter,{'signature':'is_letter(Int)->Bool','args':[{'ref':'Solve.char'}]})
        self.assertFalse(any(t['id'].endswith('.IsLetter') for t in document['templates']))
        self.assertEqual([p['expression']['constant']['range'] for p in nts['Solve.char']['alternatives']],contract['chars'])
        self.assertEqual(document['templates'][0]['body']['captures'],[{'input':'scalar0'}])
        self.assertIn({'name':'scalar0','type':'Int'},nts['Solve.char']['scope'])
        self.assertNotIn({'name':'scalar0','type':'Int'},nts['Divide.int']['scope'])
        with self.assertRaisesRegex(ValueError,'task mismatch'):
            definition(ROOT,'wrong',contract=contract)

    def test_stock_source_values_are_demand_evaluated_and_clamped(self):
        contract=dict(task='stock_with_cooldown',scheme='dynamic-programming',carrier='int',
            scalars=[],base_size=1,ints=[['0','2']],chars=[],dp_dimensions=2,
            dp_deps=[[1,0],[1,1],[2,-1]],sequence_input=1,dp_value_at=0,dp_value_offset=0,
            phases=[dict(start='int',productions=[production(1,'int'),production(8,'int'),production(10,'int')]),
                dict(start='int',productions=[production(7,'int')])])
        document=definition(ROOT,'stock_with_cooldown',contract=contract)
        root=document['templates'][0]
        self.assertEqual([len(h['scope']) for h in root['holes']],[4,7])
        self.assertNotIn('source_v',str(root))
        self.assertEqual(root['body']['args'][3],{'hole':'solve'})
        solve=next(nt for nt in document['nonterminals'] if nt['id']=='Solve.int')
        literal,state,price=[p['expression'] for p in solve['alternatives']]
        self.assertEqual(literal,{'constant':{'type':'Int','range':['0','2']}})
        self.assertEqual(state,{'signature':'min(Int,Int)->Int','args':[{'bound':'n'},{'bound':'extent'}]})
        self.assertEqual(price['signature'],'if(Bool,Int,Int)->Int')
        self.assertNotIn('index(',str(root))
        # Independent interpretation of this small clamp expression at boundaries.
        def evaluate(e,n,xs):
            if 'bound' in e:return {'n':n,'xs':xs,'extent':len(xs)-1}[e['bound']]
            if 'constant' in e:return int(e['constant']['values'][0])
            op=e['signature'].split('(')[0]
            if op=='if':return evaluate(e['args'][1 if evaluate(e['args'][0],n,xs) else 2],n,xs)
            args=[evaluate(a,n,xs) for a in e['args']]
            if op=='eq':return args[0]==args[1]
            if op=='min':return min(args)
            if op=='max':return max(args)
            if op=='sub':return args[0]-args[1]
            if op=='len':return len(args[0])
            if op=='index':return args[0][args[1]]
            raise AssertionError(op)
        self.assertEqual(evaluate(price,0,[]),0)
        for n,expected in [(-1,10),(0,10),(1,20),(2,30),(3,30),(99,30)]:
            self.assertEqual(evaluate(price,n,[10,20,30]),expected)
            self.assertEqual(evaluate(state,n,[10,20,30]),min(n,2))

    def test_coordinate_dependencies_preserve_source_order_and_dimension(self):
        contract=dict(dp_dimensions=2,dp_deps=[[-1,0],[0,-1]],sequence_input=0)
        body=template(ROOT,'fibonacci_array',contract)['body']
        plan=body['structured']['plan']
        self.assertEqual([[int(s['offset']) for s in request['states']] for request in plan['requests']],contract['dp_deps'])
        self.assertEqual(plan['coordinate_slots'],[0,1])
        self.assertEqual(plan['limits']['cells'],169)
        self.assertEqual(body['args'][:2],[{'input':'start_a'},{'input':'start_b'}])
        self.assertEqual([p['argument'] for p in body['phases']],[2,3,4,5])
        interval=template(ROOT,'subregion_sum',dict(dp_dimensions=2,dp_deps=[[0,-1]],sequence_input=1))['body']
        self.assertEqual(interval['structured']['plan']['limits']['cells'],20)
        stock=template(ROOT,'stock_with_cooldown',dict(dp_dimensions=2,dp_deps=[[1,0],[1,1],[2,-1]],sequence_input=1))['body']
        self.assertEqual(stock['structured']['plan']['coordinate_rank'][0]['direction'],-1)
        combine=next(p for p in stock['phases'] if any(b['bank']=='result' for b in p['bindings']))
        self.assertEqual(combine['argument'],7)
        self.assertEqual(stock['args'][7],{'hole':'transition'})
        self.assertEqual(stock['args'][3],{'hole':'solve'})
        self.assertTrue(all(r['states'][1]['kind']=='expression' for r in stock['structured']['plan']['requests']))
        self.assertTrue(all(r['states'][1]['offset']=='0' and r['states'][1]['source_state']==0 for r in stock['structured']['plan']['requests']))

if __name__=='__main__':
    unittest.main()
