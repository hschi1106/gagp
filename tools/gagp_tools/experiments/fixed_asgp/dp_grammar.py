"""Bounded coordinate plans for source recurrence shapes (not task solutions)."""
import copy
import json
from .grammar import bound, call, constant

# Source task/dp_problems.cc defines these fixed base predicates and domains.
SHAPES = {'fibonacci': (30,'one'), 'tribonacci':(25,'two'),
          'subregion_sum':(19,'interval'), 'fibonacci_array':(12,'axes'),
          'stock_with_cooldown':(14,'time')}

def template(root, task, contract):
    if task not in SHAPES:
        raise ValueError('unsupported recurrence: '+task+' (requires clamped coordinate transitions)')
    upper, predicate = SHAPES[task]
    dim=contract['dp_dimensions']
    package=json.loads((root/'configs/grammar/packages/dp1d_int.json').read_text())
    body=copy.deepcopy(package['nonterminals'][0]['alternatives'][1]['expression'])
    plan=body['structured']['plan']
    plan['state_types']=['Int']*dim
    plan['parameter_types']=['IntList'] if contract['sequence_input'] else []
    prototype=copy.deepcopy(plan['requests'][0]['states'][0])
    plan['requests']=[]
    for dep in contract['dp_deps']:
        states=[]
        for i in range(dim):
            state=copy.deepcopy(prototype);state.update(source_state=i,offset=str(dep[i]));states.append(state)
        plan['requests'].append({'states':states})
    plan['coordinate_slots']=list(range(dim))
    plan['coordinate_rank']=[{'coordinate':i,'direction':1} for i in range(dim)]
    domain=copy.deepcopy(plan['coordinate_domains'][0]);domain['upper']['literal']=str(upper)
    plan['coordinate_domains']=[copy.deepcopy(domain) for _ in range(dim)]
    plan['limits'].update(frames=127,cells=(upper+1)**dim)
    body['captures']=[{'input':'source'}] if contract['sequence_input'] else []
    state_scope=[{'name':('n','j')[i],'type':'Int'} for i in range(dim)]
    bindings=[{'bank':'state','slot':i,'name':s['name']} for i,s in enumerate(state_scope)]
    scope=state_scope+([{'name':'xs','type':'IntList'}] if contract['sequence_input'] else [])
    phase_bindings=bindings+([{'bank':'parameter','slot':0,'name':'xs'}] if contract['sequence_input'] else [])
    results=[{'name':f'd{i+1}','type':'Int'} for i in range(len(plan['requests']))]
    if predicate in ('one','two'):base=call('le(Int,Int)->Bool',bound('n'),constant(1 if predicate=='one' else 2))
    elif predicate=='interval':base=call('le(Int,Int)->Bool',bound('j'),bound('n'))
    else:base=call('if(Bool,Bool,Bool)->Bool',call('eq(Int,Int)->Bool',bound('n'),constant(0)),constant(True,'Bool'),call('eq(Int,Int)->Bool',bound('j'),constant(0)))
    body['args']=[{'input':f'start_{axis}'} for axis in ('a','b')[:dim]]+[base,{'hole':'solve'},{'hole':'transition'},constant(0)]
    body['phases']=[{'argument':dim,'bindings':bindings},
        {'argument':dim+1,'bindings':phase_bindings},
        {'argument':dim+2,'bindings':phase_bindings+[{'bank':'result','slot':i,'name':s['name']} for i,s in enumerate(results)]},
        {'argument':dim+3,'bindings':[]}]
    if predicate=='time':
        plan['parameter_types'].append('Int')
        body['captures'].append({'input':'extent_a'})
        scope.append({'name':'extent','type':'Int'})
        phase_bindings.append({'bank':'parameter','slot':1,'name':'extent'})
        # Each nonterminal frame advances time strictly; holding-state requests
        # clamp independently. Source terminals bypass the memo table.
        plan['coordinate_rank'][0]['direction']=-1
        plan['coordinate_domains'][1]['upper']['literal']='1'
        plan['limits']['cells']=(upper+1)*2
        plan['request_expression_types']=['Int']*len(plan['requests'])
        for i,(request,dep) in enumerate(zip(plan['requests'],contract['dp_deps'])):
            request['states'][1].update(kind='expression',expression=i,source_state=0,offset='0')
            body['args'].append(call('min(Int,Int)->Int',constant(1),call('max(Int,Int)->Int',constant(0),call('add(Int,Int)->Int',bound('j'),constant(dep[1])))))
            body['phases'].append({'argument':dim+4+i,'bindings':bindings})
        body['args'][dim]=call('ge(Int,Int)->Bool',bound('n'),bound('extent'))
        body['phases'][0]['bindings']=phase_bindings
        body['phases'][2]['bindings']=phase_bindings+[{'bank':'result','slot':i,'name':s['name']} for i,s in enumerate(results)]
    return {'id':f'Fixed.DP.{task}','type':'Int','scope':[],
        'holes':[{'id':'solve','type':'Int','scope':scope}, {'id':'transition','type':'Int','scope':scope+results}], 'body':body}
