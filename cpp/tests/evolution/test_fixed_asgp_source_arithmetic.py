"""CPU-only parity fixtures for source arithmetic and nested hygienic lets."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
import unittest

BINARY=Path(sys.argv.pop(1)).resolve()
ROOT=Path(sys.argv.pop(1)).resolve()
sys.path.insert(0,str(ROOT/'tools'))
from gagp_tools.experiments.fixed_asgp.grammar import definition

def literal(value):
    return [[1,1,0,1,str(value)]]

def arithmetic(op,left,right):
    return [[22,1,op,1+len(left)+len(right),'0']]+left+right

class TestProtectedSourceArithmetic(unittest.TestCase):
    def call(self,*args,env=None):
        result=subprocess.run([str(BINARY),*map(str,args)],env=env,capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_signed_modulo_protected_overflow_and_nested_operands(self):
        with tempfile.TemporaryDirectory() as folder:
            folder=Path(folder);contract_path=folder/'contract.json';source_path=folder/'source.json'
            self.call('contract','min',contract_path)
            self.call('freeze','min',source_path,env=dict(os.environ,GAGP_FREEZE_POPULATION='8'))
            contract=json.loads(contract_path.read_text())
            # Widen only this synthetic fixture's literal pool to test int64
            # boundaries directly. No benchmark task or frozen input is edited.
            contract['ints']=[[str(-(1<<63)),str((1<<63)-1)]]
            grammar_path=folder/'grammar.json';grammar_path.write_text(json.dumps(definition(ROOT,'min',contract=contract)))
            expressions=[arithmetic(4,literal(-5),literal(3)),arithmetic(4,literal(5),literal(-3)),
                arithmetic(4,literal(-5),literal(-3)),arithmetic(4,literal(7),literal(0)),
                arithmetic(3,literal(-(1<<63)),literal(-1)),arithmetic(4,literal(-(1<<63)),literal(-1)),
                arithmetic(4,arithmetic(3,literal(-17),literal(3)),literal(-3)),
                arithmetic(3,literal(17),arithmetic(4,literal(-8),literal(3)))]
            source=json.loads(source_path.read_text())
            source['population']=[[expr,literal(0),[[7,1,0,1,'0']]] for expr in expressions]
            case=dict(source['cases'][0]);case['input']=['1'];case['expected']='1'
            source['cases']=[case]*1024;source_path.write_text(json.dumps(source))
            self.call('prepare',source_path,grammar_path,8,folder/'prepared.json')
            self.call('sampled-audit',source_path,grammar_path,folder/'audit.json')
            result=json.loads((folder/'audit.json').read_text())
            for mode, name in [('search_gagp_cpu','GAGP_SEARCH_PENALTY'), ('snapshot','GAGP_SNAPSHOT_PENALTY')]:
                rejected=subprocess.run([str(BINARY),'measure',str(source_path),str(folder/'prepared.json'),
                    str(grammar_path),'8',mode,str(folder/'rejected.json')],
                    env=dict(os.environ,GAGP_BM_ALIGNED_FITNESS='1',**{name:'1'}),capture_output=True,text=True,timeout=30)
                self.assertNotEqual(rejected.returncode,0)
                self.assertIn('aligned fitness requires',rejected.stdout+rejected.stderr)
            self.assertEqual(result['mismatches'],0)
            self.assertEqual(result['sampled_programs'],8)
            self.assertTrue(all(row['source_errors']==row['native_errors']==0 for row in result['programs']))

if __name__=='__main__':unittest.main()
