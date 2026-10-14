"""Offline syntactic coverage only; never grants execution/grammar authority."""
import collections
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[5]
descriptors = (ROOT/'cpp/src/evolution/node_descriptor.cpp').read_text()
header = (ROOT/'cpp/include/gagp/evolution/ast_program.hpp').read_text()
body = header.split('enum class NodeKind {', 1)[1].split('};', 1)[0]
body = re.sub(r'//[^\n]*', '', body)
ids = {}; value = 0
for token in body.split(','):
    token = token.strip()
    if not token: continue
    if '=' in token:
        name, raw = token.split('='); value = int(raw.strip())
    else: name = token
    ids[name.strip()] = value; value += 1
arity = {}
for kind, count in re.findall(r'NODE\((\w+),\s*"[^"]+",\s*\w+,\s*(\d+)', descriptors):
    arity[ids[kind]] = int(count)
for kind, count in re.findall(r'BUILTIN\((\w+),\s*"[^"]+",\s*(\d+)', descriptors):
    arity[ids[kind]] = int(count)
scalar_ops = set(range(9, 31)) - {22, 23, 24}
branches = {22, 23, 24}

def scan(path):
    programs = json.loads(path.read_text())['programs']
    total = collections.Counter(programs=len(programs))
    for ast in programs:
        nodes = ast['nodes']; parsed = {}
        intern = {}
        def visit(index):
            n = nodes[index]; k = n['kind']; end = index + 1; children = []
            for _ in range(n['i0'] if k == 77 else arity[k]):
                child, end = visit(end); children.append(child)
            size = end-index
            constant = k == 7 and ast['consts'][n['i0']]['type'] in ('int','bool','float')
            constant |= k in scalar_ops and all(c['constant'] for c in children)
            payload = json.dumps(ast['consts'][n['i0']],sort_keys=True) if k == 7 else (n['i0'],n['i1'])
            key = (k,payload,tuple(c['id'] for c in children))
            signature = intern.setdefault(key,len(intern))
            result = dict(id=signature,size=size,constant=constant,branch=k in branches or any(c['branch'] for c in children),children=children)
            parsed[index] = result
            return result,end
        _,end = visit(0)
        assert end == len(nodes)
        for region in ast.get('bounded_region_specs',[]):
            start = region['node_index']; args = parsed[start]['children']
            for phase in region['phases']:
                expr = args[phase['argument']]; total['phase_nodes'] += expr['size']; total['phases'] += 1
                def fold(c):
                    return c['size']-1 if c['constant'] else sum(fold(x) for x in c['children'])
                savings = fold(expr); total['constant_candidate_nodes_removed'] += savings
                if not expr['branch']: total['straightline_constant_candidate_nodes_removed'] += savings
                seen = set()
                def repeated(c):
                    if c['size'] >= 4 and c['id'] in seen: return c['size']-1
                    seen.add(c['id']); return sum(repeated(x) for x in c['children'])
                total['syntactic_repeated_nodes_replaced_by_reference'] += repeated(expr)
    return dict(total)

if __name__ == '__main__':
    out = {'limits':'Syntactic counts only, not dynamic-work/speedup bounds. No fuel, numeric, error, binding or liveness proof. Repeated-subtree references preserve logical occurrences only in a separate execution representation.', 'snapshots':[]}
    for task in ('house_robber','median'):
        for label,path in [('initial',ROOT/f'logs/fixed-asgp/artifacts/{task}.p1024.prepared.json'),('gen128_seed0',ROOT/f'logs/optimization/astra-20261014/final/long/{task}.gpu_overlap.s0.r0.final.json')]:
            out['snapshots'].append(dict(task=task,snapshot=label,path=str(path),**scan(path)))
    print(json.dumps(out,indent=2))
