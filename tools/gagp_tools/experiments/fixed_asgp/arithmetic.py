"""Map source protected arithmetic to explicit native truncating primitives."""


def protect(document):
    """Keep source operands and one physical node per arithmetic operation."""
    signatures = {
        'idiv0(Int,Int)->Int': 'trunc_div0(Int,Int)->Int',
        'imod0(Int,Int)->Int': 'trunc_mod0(Int,Int)->Int',
    }
    for nt in document['nonterminals']:
        for alternative in nt['alternatives']:
            expression = alternative['expression']
            signature = expression.get('signature')
            if signature in signatures:
                expression['signature'] = signatures[signature]
    return document
