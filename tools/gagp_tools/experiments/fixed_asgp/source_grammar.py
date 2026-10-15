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
TYPES = {'int':'Int', 'bool':'Bool', 'char':'Int', 'intList':'IntList', 'charList':'IntList', 'tuple':'IntList'}

def definition(root: Path, task: str, contract: dict) -> dict:
    if contract['task'] != task:
        raise ValueError('source contract task mismatch')
    if task == 'house_robber':
        from .grammar import definition as legacy_definition
        return legacy_definition(root, task)
    dp = contract['scheme']=='dynamic-programming'
    carrier = TYPES[contract['carrier']]
    scalars = [{'name':f'scalar{i}','type':'Int'} for i,_ in enumerate(contract['scalars'])]
    if dp:
        from .dp_grammar import template as dp_template
        template=dp_template(root,task,contract)
        phase_names=('Solve','Transition')
    else:
        package = json.loads((root/'configs/grammar/packages/dc_intlist_int.json').read_text())
        template = copy.deepcopy(next(t for t in package['templates'] if t['id']==f'Package.DC.IntList.{carrier}'))
        body = template['body']
        body['structured']['plan']['limits']['frames'] = 127
        body['phases'][0]['bindings'] = [{'bank':'measure','slot':0,'name':'n'}]
        body['args'][2] = call('le(Int,Int)->Bool',bound('n'),constant(contract['base_size']))
        body['structured']['plan']['parameter_types'] = ['Int'] * len(scalars)
        body['captures'] = [{'input':s['name']} for s in scalars]
        body['phases'][1]['bindings'] += [{'bank':'parameter','slot':i,'name':s['name']} for i,s in enumerate(scalars)]
        template['holes'][1]['scope'] += scalars
        phase_names=('Solve','Divide','Combine')
    nts=[]
    index_templates=[]
    for phase_index, phase in enumerate(phase_names):
        scope=template['holes'][phase_index+(0 if dp else 1)]['scope']
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
                    elif kind==7:e=bound(f'd{op+1}' if dp else ('left','right')[op])
                    elif kind==8:e=bound('source_n' if task=='stock_with_cooldown' else 'n')
                    elif kind==9:e=bound('j')
                    elif kind==10:
                        coordinate=bound('n' if contract['dp_value_at']==1 else 'j')
                        offset=contract['dp_value_offset']
                        e=bound('source_v') if task=='stock_with_cooldown' else call('index(IntList,Int)->Int',bound('xs'),coordinate) if task=='subregion_sum' else call('index(IntList,Int)->Int',bound('xs'),call('min(Int,Int)->Int',call('sub(Int,Int)->Int',call('len(IntList)->Int',bound('xs')),constant(1)),call('max(Int,Int)->Int',call('add(Int,Int)->Int',coordinate,constant(offset)),constant(0))))
                    elif kind==11:e=call('not(Bool)->Bool',*args)
                    elif kind==12:
                        template_id=f'Fixed.{phase}.IsLetter'
                        char={'hole':'char'}
                        def between(lo,hi):return call('if(Bool,Bool,Bool)->Bool',call('ge(Int,Int)->Bool',char,constant(lo)),call('le(Int,Int)->Bool',char,constant(hi)),constant(False,'Bool'))
                        if not any(t['id']==template_id for t in index_templates):
                            index_templates.append({'id':template_id,'type':'Bool','scope':copy.deepcopy(scope),
                                'holes':[{'id':'char','type':'Int','scope':copy.deepcopy(scope)}],
                                'body':call('if(Bool,Bool,Bool)->Bool',between(65,90),constant(True,'Bool'),between(97,122))})
                        e={'template':template_id,'holes':{'char':args[0]}}
                    elif kind==13:e=call('index(IntList,Int)->Int',args[0],constant(0))
                    elif kind==14:e=bound('lo')
                    elif kind==15:
                        template_id=f'Fixed.{phase}.Index'
                        if not any(t['id']==template_id for t in index_templates):
                            index={'hole':'index'}
                            index_templates.append({'id':template_id,'type':'Int','scope':copy.deepcopy(scope),
                                'holes':[{'id':'index','type':'Int','scope':copy.deepcopy(scope)}],
                                'body':call('if(Bool,Int,Int)->Int',call('lt(Int,Int)->Bool',index,constant(0)),
                                    call('index(IntList,Int)->Int',bound('xs'),call('len(IntList)->Int',bound('xs'))),
                                    call('index(IntList,Int)->Int',bound('xs'),index))})
                        e={'template':template_id,'holes':{'index':args[0]}}
                    elif kind==16:e=call('singleton(Int)->IntList',*args)
                    elif kind in (17,18,19):
                        e=call('index(IntList,Int)->Int',args[0],constant(kind-17))
                        if kind==17:e=call('ne(Int,Int)->Bool',e,constant(0))
                    elif kind==26:e=call('concat(IntList,IntList)->IntList',call('singleton(Int)->IntList',call('if(Bool,Int,Int)->Int',args[0],constant(1),constant(0))),call('concat(IntList,IntList)->IntList',call('singleton(Int)->IntList',args[1]),call('singleton(Int)->IntList',args[2])))
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
    holes={} if dp else {'source':{'input':'source'}}
    holes.update({p.lower():{'ref':f'{p}.{contract["phases"][i]["start"]}'} for i,p in enumerate(phase_names)})
    nts.insert(0,{'id':'Root','type':carrier,'scope':[],'alternatives':[{'id':'scheme','weight':1,'expression':{'template':template['id'],'holes':holes}}]})
    return {'format_version':'grammar-definition-v2','inputs':[{'name':'source','type':'IntList'}]+scalars+([{'name':n,'type':'Int'} for n in ('start_a','start_b','extent_a','extent_b')] if dp else []),
        'entry':{'nonterminal':'Root','type':carrier},'search_limits':{'max_nodes':1024,'max_depth':48},
        'execution_limits':{'fuel':2000000},'templates':[template]+index_templates,'nonterminals':nts}
