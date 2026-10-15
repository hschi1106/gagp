"""Translate the source's reachable productions, preserving separate Char types.

Char/CharList use integer payloads internally, but remain distinct nonterminals.
This preserves the source grammar's typing restrictions during native search.
"""
from __future__ import annotations
import copy
import json
from pathlib import Path
from .grammar import bound, call, constant

PAPER_TASKS = ('filter_odd', 'sum_of_elements', 'is_sorted', 'index_of_element',
    'all_equal', 'cumulative_sum', 'fuel_cost', 'luhn', 'min', 'mpi', 'count_odds',
    'last_index_of_zero', 'small_or_large', 'for_loop_index', 'double_letters',
    'replace_space_with_newline', 'even_squares', 'sum_of_squares',
    'negative_to_zero', 'scrabble_score', 'grade', 'median', 'smallest', 'syllables',
    'fibonacci', 'tribonacci', 'house_robber', 'subregion_sum', 'fibonacci_array',
    'stock_with_cooldown')
TYPES = {'int':'Int', 'bool':'Bool', 'char':'Int', 'intList':'IntList', 'charList':'IntList'}

def definition(root: Path, task: str, contract: dict) -> dict:
    if contract['task'] != task:
        raise ValueError('source contract task mismatch')
    if contract['scheme'] != 'divide-and-conquer':
        raise ValueError('unsupported source recurrence shape: ' + task)
    if contract['carrier'] == 'tuple':
        raise ValueError('unsupported tuple carrier/projection: ' + task)
    carrier = TYPES[contract['carrier']]
    package = json.loads((root/'configs/grammar/packages/dc_intlist_int.json').read_text())
    template = copy.deepcopy(next(t for t in package['templates'] if t['id']==f'Package.DC.IntList.{carrier}'))
    body = template['body']
    body['structured']['plan']['limits']['frames'] = 127
    body['phases'][0]['bindings'] = [{'bank':'measure','slot':0,'name':'n'}]
    body['args'][2] = call('le(Int,Int)->Bool',bound('n'),constant(contract['base_size']))
    scalars = [{'name':f'scalar{i}','type':'Int'} for i,_ in enumerate(contract['scalars'])]
    body['structured']['plan']['parameter_types'] = ['Int'] * len(scalars)
    body['captures'] = [{'input':s['name']} for s in scalars]
    body['phases'][1]['bindings'] += [{'bank':'parameter','slot':i,'name':s['name']} for i,s in enumerate(scalars)]
    template['holes'][1]['scope'] += scalars
    nts=[]
    for phase_index, phase in enumerate(('Solve','Divide','Combine')):
        scope=template['holes'][phase_index+1]['scope']
        productions=contract['phases'][phase_index]['productions']
        groups={}
        for prod in productions:
            kind, op, typ = prod['kind'], prod['op'], prod['type']
            args=[{'ref':f'{phase}.{t}'} for t in prod['children']]
            if kind in (1,2):
                expressions=[{'constant':{'type':'Int','range':r}} for r in contract['ints' if kind==1 else 'chars']]
            else:
                if kind==0: exprs=[constant(False,'Bool'),constant(True,'Bool')]
                else:
                    if kind==3: e=bound('xs')
                    elif kind==4:e=constant([],'IntList')
                    elif kind==5:e=bound('n')
                    elif kind==6:e=bound(f'scalar{op}')
                    elif kind==7:e=bound(('left','right')[op])
                    elif kind==11:e=call('not(Bool)->Bool',*args)
                    elif kind==12:
                        def between(lo,hi):return call('if(Bool,Bool,Bool)->Bool',call('ge(Int,Int)->Bool',args[0],constant(lo)),call('le(Int,Int)->Bool',args[0],constant(hi)),constant(False,'Bool'))
                        e=call('if(Bool,Bool,Bool)->Bool',between(65,90),constant(True,'Bool'),between(97,122))
                    elif kind==13:e=call('index(IntList,Int)->Int',args[0],constant(0))
                    elif kind==14:e=bound('lo')
                    elif kind==15:e=call('index(IntList,Int)->Int',bound('xs'),args[0])
                    elif kind==16:e=call('singleton(Int)->IntList',*args)
                    elif kind==20:e=call(f"{('lt','eq','gt','le','ge','ne')[op]}(Int,Int)->Bool",*args)
                    elif kind==21:e=call('if(Bool,Bool,Bool)->Bool',args[0],args[1] if op==0 else constant(True,'Bool'),constant(False,'Bool') if op==0 else args[1])
                    elif kind==22:e=call(f"{('add','sub','mul','idiv0','imod0')[op]}(Int,Int)->Int",*args)
                    elif kind==23:e=call('eq(Int,Int)->Bool',*args)
                    elif kind==24:e=call('concat(IntList,IntList)->IntList',*args)
                    elif kind==25:e=call(f'if(Bool,{TYPES[typ]},{TYPES[typ]})->{TYPES[typ]}',*args)
                    else:raise ValueError(f'unsupported ASGP production {kind}')
                    exprs=[e]
                expressions=exprs
            groups.setdefault(typ,[]).extend(expressions)
        for typ,expressions in groups.items():
            nts.append({'id':f'{phase}.{typ}','type':TYPES[typ],'scope':copy.deepcopy(scope),
                'alternatives':[{'id':f'p{i}','weight':1,'expression':e} for i,e in enumerate(expressions)]})
    holes={'source':{'input':'source'}}
    holes.update({p.lower():{'ref':f'{p}.{contract["phases"][i]["start"]}'} for i,p in enumerate(('Solve','Divide','Combine'))})
    nts.insert(0,{'id':'Root','type':carrier,'scope':[],'alternatives':[{'id':'scheme','weight':1,'expression':{'template':template['id'],'holes':holes}}]})
    return {'format_version':'grammar-definition-v2','inputs':[{'name':'source','type':'IntList'}]+scalars,
        'entry':{'nonterminal':'Root','type':carrier},'search_limits':{'max_nodes':1024,'max_depth':48},
        'execution_limits':{'fuel':2000000},'templates':[template],'nonterminals':nts}
