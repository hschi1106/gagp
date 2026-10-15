"""Opt-in exact source-production height charges for translated ASGP grammars.

This changes search resource accounting, not grammar membership or execution.
Only the translator's one-production-per-alternative representation is supported.
The native invariant-root certificate remains a separate mandatory admission gate.
"""
from __future__ import annotations

import copy


def annotate(document: dict) -> dict:
    """Return a charged copy; leave frozen source documents untouched.

    Source production roots count one level and administrative lowering nodes
    count zero. AND/OR's inserted Boolean literal counts one: each real source
    child already has height >= 1, so this does not change aggregate height and
    makes overlapping native derivations have invariant per-occurrence charges.
    """
    result = copy.deepcopy(document)

    def charge(expr: dict, depth: int = 0, reset: bool = False) -> None:
        if any(key in expr for key in ('ref', 'hole', 'template')):
            if depth or reset:
                raise ValueError('source production root must materialize a native node')
            return
        if 'resource_charge' in expr and expr['resource_charge'] not in (
                {'nodes': 0, 'depth': 0, 'resets_depth': False},
                {'nodes': 1, 'depth': 1, 'resets_depth': False}):
            raise ValueError('source depth annotation requires source/default administrative charges')
        expr['resource_charge'] = {'nodes': 1, 'depth': depth, 'resets_depth': reset}
        for child in expr.get('args', []):
            charge(child)

    for template in result.get('templates', []):
        body = template['body']
        if 'structured' not in body:
            raise ValueError('source depth supports only the fixed structured scaffold')
        charge(body, reset=True)
    for nt in result['nonterminals']:
        if nt['id'] == result['entry']['nonterminal']:
            # Template invocations and ref/hole substitution have no materialized
            # charges. The template body resets the enclosing native envelope.
            continue
        for alternative in nt['alternatives']:
            expr = alternative['expression']
            charge(expr, depth=1)
            if expr.get('signature') == 'if(Bool,Bool,Bool)->Bool':
                # Only AND/OR lowering inserts a fixed Bool child in this source
                # translator; general source IF has three child references.
                for child in expr.get('args', [])[1:]:
                    if child.get('constant', {}).get('type') == 'Bool':
                        child['resource_charge']['depth'] = 1
    return result
