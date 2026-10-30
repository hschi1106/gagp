"""Materialize the integer DC grammar used by the ASGP quick comparison."""
import json
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
package = json.loads((root / 'configs/grammar/packages/dc_intlist_int.json').read_text())
template = next(t for t in package['templates'] if t['id'] == 'Package.DC.IntList.Int')
template['body']['structured']['plan']['limits']['frames'] = 128
# Keep the native DC skeleton and phase visibility; phase rules below mirror ASGP.
def const(t, v):
    return {'constant': {'type': t, 'values': [v]}}
def sig(s, *args):
    return {'signature': s, 'args': list(args)}
def remainder(a, b):
    return sig('imod0(Int,Int)->Int',a,b)
def ref(p, t):
    return {'ref': f'{p}.{t}'}
def bound(n):
    return {'bound': n}
nts = []
for phase, scope in [('Solve', [('xs','IntList'), ('n','Int'), ('lo','Int')]),
                     ('Divide', [('n','Int')]), ('Combine',[('left','Int'),('right','Int')])]:
    i, b = ref(phase,'Int'), ref(phase,'Bool')
    ints = [{'constant': {'type':'Int', 'range':['0','100']}}]
    if phase != 'Combine': ints.append(bound('n'))
    else: ints.extend([bound('left'),bound('right')])
    if phase == 'Solve':
        ints.extend([sig('index(IntList,Int)->Int', bound('xs'), const('Int','0')),
                     sig('index(IntList,Int)->Int', bound('xs'), i)])
    ints.extend(sig(f'{op}(Int,Int)->Int', i, i) for op in ['add','sub','mul','idiv0'])
    ints.append(remainder(i,i))
    ints.append(sig('if(Bool,Int,Int)->Int',b,i,i))
    bools = [const('Bool',False),const('Bool',True)]
    bools.extend(sig(f'{op}(Int,Int)->Bool',i,i) for op in ['lt','eq','gt','le','ge','ne'])
    bools.extend([sig('if(Bool,Bool,Bool)->Bool',b,b,const('Bool',False)),
                  sig('if(Bool,Bool,Bool)->Bool',b,const('Bool',True),b),
                  sig('not(Bool)->Bool',b)])
    for ty, expressions in [('Int',ints),('Bool',bools)]:
        nts.append({'id': f'{phase}.{ty}', 'type':ty,
                    'scope':[{'name':n,'type':t} for n,t in scope],
                    'alternatives':[{'id':f'p{k}','weight':4 if 'constant' in e or 'bound' in e else 1,'expression':e} for k,e in enumerate(expressions)]})
entry = {'template':template['id'],'holes':{'source':{'input':'source'},
    'solve':ref('Solve','Int'),'divide':ref('Divide','Int'),'combine':ref('Combine','Int')}}
nts.insert(0,{'id':'Root','type':'Int','scope':[],'alternatives':[{'id':'dc','weight':1,'expression':entry}]})
definition = {'format_version':'grammar-definition-v2','inputs':[{'name':'source','type':'IntList'}],
    'entry':{'nonterminal':'Root','type':'Int'}, 'search_limits':{'max_nodes':1024,'max_depth':48},
    'execution_limits':{'fuel':2000000},'templates':[template],'nonterminals':nts}
Path(sys.argv[1]).write_text(json.dumps(definition,indent=2)+'\n')
