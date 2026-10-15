"""Protected source arithmetic lowered through hygienic single-evaluation lets."""
import copy
from .grammar import bound, call, constant


def template(identifier, scope, modulo):
    a,b=bound('source_lhs'),bound('source_rhs')
    if modulo:
        # Truncating remainder, including both source protected exceptional cases.
        result=call('if(Bool,Int,Int)->Int',call('eq(Int,Int)->Bool',b,constant(0)),constant(0),
            call('sub(Int,Int)->Int',a,call('mul(Int,Int)->Int',call('idiv0(Int,Int)->Int',a,b),b)))
    else:
        overflow=call('if(Bool,Bool,Bool)->Bool',call('eq(Int,Int)->Bool',a,constant(-(1<<63))),
            call('eq(Int,Int)->Bool',b,constant(-1)),constant(False,'Bool'))
        result=call('if(Bool,Int,Int)->Int',overflow,constant(0),call('idiv0(Int,Int)->Int',a,b))
    inner=call('let(Int,Int)->Int',{'hole':'right'},result);inner['bind']={'1':['source_rhs']}
    outer=call('let(Int,Int)->Int',{'hole':'left'},inner);outer['bind']={'1':['source_lhs']}
    def scaffold(expr):
        if any(k in expr for k in ('signature','constant','bound')):
            expr['resource_charge']={'nodes':0,'depth':0,'resets_depth':False}
        for arg in expr.get('args',[]):scaffold(arg)
    scaffold(outer)
    outer['resource_charge']={'nodes':1,'depth':1,'resets_depth':False}
    return {'id':identifier,'type':'Int','scope':copy.deepcopy(scope),
        'holes':[{'id':name,'type':'Int','scope':copy.deepcopy(scope)} for name in ('left','right')], 'body':outer}


def protect(document):
    """Replace only source productions; fixed recurrence scaffolding is untouched."""
    templates=[]
    for nt in document['nonterminals']:
        phase=nt['id'].split('.')[0]
        for alternative in nt['alternatives']:
            expr=alternative['expression']
            signature=expr.get('signature')
            if signature not in ('idiv0(Int,Int)->Int','imod0(Int,Int)->Int'):
                continue
            modulo=signature.startswith('imod0')
            identifier=f'Fixed.{phase}.Protected'+('Mod' if modulo else 'Div')
            if not any(t['id']==identifier for t in templates):
                templates.append(template(identifier,nt['scope'],modulo))
            alternative['expression']={'template':identifier,'holes':dict(zip(('left','right'),expr['args']))}
    document['templates']+=templates
    return document
