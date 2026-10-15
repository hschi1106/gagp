"""The optional list-combine contract is explicit and source trees stay intact."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from gagp_tools.experiments.fixed_asgp.source_grammar import definition

spec = importlib.util.spec_from_file_location('list_overlay', ROOT / 'cpp/src/bench/fixed_asgp/list_combine_overlay.py')
overlay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(overlay)


class TestListCombineContract(unittest.TestCase):
    def contract(self):
        return dict(task='cumulative_sum', scheme='divide-conquer', carrier='intList',
                    scalars=[], base_size=1, ints=[[0, 2]], chars=[],
                    grammar_extension='list-combine-v1', phases=[
                        dict(start='intList', productions=[dict(kind=3,op=0,type='intList',children=[])]),
                        dict(start='int', productions=[dict(kind=1,op=0,type='int',children=[])]),
                        dict(start='intList', productions=[
                            dict(kind=7,op=0,type='intList',children=[]),
                            dict(kind=13,op=1,type='int',children=['intList']),
                            dict(kind=16,op=0,type='intList',children=['int'])])])

    def test_last_is_single_child_existing_index(self):
        grammar = definition(ROOT, 'cumulative_sum', self.contract())
        ints = next(nt for nt in grammar['nonterminals'] if nt['id']=='Combine.int')
        expression = ints['alternatives'][0]['expression']
        self.assertEqual(expression['signature'], 'index(IntList,Int)->Int')
        self.assertEqual(expression['args'][0], {'ref':'Combine.intList'})
        self.assertEqual(expression['args'][1]['constant']['values'], ['-1'])

    def test_last_rejected_under_original_contract(self):
        contract = self.contract(); contract['grammar_extension']='none'
        with self.assertRaisesRegex(ValueError, 'list-combine-v1'):
            definition(ROOT, 'cumulative_sum', contract)

    def test_incompatible_source_does_not_write_overlay(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary); source=root/'source'; destination=root/'generated'
            (source/'lang').mkdir(parents=True)
            original=source/'lang/grammar.cc'; original.write_text('unsupported source')
            with self.assertRaisesRegex(ValueError, 'source anchor mismatch'):
                overlay.generate(source, destination)
            self.assertEqual(original.read_text(), 'unsupported source')
            self.assertFalse(destination.exists())


if __name__ == '__main__':
    unittest.main()
