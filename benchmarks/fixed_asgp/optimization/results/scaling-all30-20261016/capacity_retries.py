"""Separate capacity-config runs; never modify the original matrix or runtime."""
from argparse import ArgumentParser, Namespace
from pathlib import Path
import json
import os
import select

from gagp_tools.experiments.fixed_asgp.low_budget import Study, read, sha, validate_search

p = ArgumentParser(description=__doc__)
p.add_argument('--matrix', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--controller-pid', type=int, required=True)
a = p.parse_args()
m = read(a.matrix / 'manifest.json')
config = Namespace(out=a.out, binary=Path(m['binary']), settings=Path('/unused'),
    stage='all30-separate-capacity-configs', generations=4, rss_gib=32,
    budget=43200, timeout=1800, native_depth=24, source_balanced=True)
# Study owns a separate copied binary and immutable settings snapshot.
settings = a.out.parent / (a.out.name + '.settings.json')
assert not settings.exists()
settings.write_text(json.dumps(m['settings'], indent=2) + '\n')
config.settings = settings
s = Study(config)
assert s.manifest['binary_sha256'] == m['binary_sha256']
s.manifest.update(results={}, population=None, binary_source_head=m.get('binary_source_head'),
    original_matrix=str((a.matrix / 'manifest.json').resolve()),
    note='Separate8/4GiB configured runs only after original16GiB failure. '
         'Exact tiled-initial-payload launch signature: representative Filter P8192 '
         'nsys cudaLaunchKernel returnValue=2. Other failures retain original classification. '
         'No profiler in these timed runs; original failed time not included in successful configured-run timing.')
s.persist()
try:
    descriptor = os.pidfd_open(a.controller_pid)
except ProcessLookupError:
    descriptor = None
poller = select.poll()
if descriptor is not None:
    poller.register(descriptor, select.POLLIN)
while True:
    m = read(a.matrix / 'manifest.json')
    for key, record in m.get('results', {}).items():
        if not record.get('complete') or key in s.manifest['results']:
            continue
        original = record.get('gpu_overlap', {})
        if original.get('status') != 'failed' or original.get('workspace_mib') != 16384:
            continue
        log_path = a.matrix / original.get('log', '/missing')
        text = log_path.read_text() if log_path.is_file() else ''
        if 'cuda tiled initial payload launch failure' not in text:
            continue
        task, pop_text = key.rsplit('.p', 1)
        population = int(pop_text)
        command = next(c['command'] for c in m['cells']
            if c.get('log') == original['log'])
        source, prepared, grammar = map(Path, command[2:5])
        result = s.manifest['results'][key] = dict(complete=False,
            original_gpu=dict(original), inputs=dict(record['inputs']), attempts=[])
        for cap in (8192, 4096):
            tag = f'p{population}.gpu_overlap.w{cap}'
            output = s.out / f'{task}.{tag}.json'
            exported = s.out / f'{task}.{tag}.final.json'
            cell = s.run(task, tag, [s.args.binary, 'measure', source, prepared,
                grammar, population, 'search_gpu_overlap', output],
                {'GAGP_GPU_PAYLOAD_WORKSPACE_MIB': str(cap), 'GAGP_SEARCH_GENERATIONS': '4',
                 'GAGP_BM_SEED': '0', 'GAGP_SEARCH_EXPORT': str(exported)})
            r = dict(status=cell['status'], workspace_mib=cap, output=str(output),
                     log=str((s.out / cell['log']).resolve()), process_s=cell.get('process_s'))
            if cell['status'] == 'ok':
                try:
                    data = read(output)
                    validate_search(data, population, 4, 'gpu_overlap')
                    r.update(evolve_wall_ms=data['evolve_wall_ms'], final_best=data['final_best'],
                             cpu_audit_mismatches=data['cpu_audit_mismatches'], sha256=sha(output))
                except (ValueError, KeyError) as error:
                    r.update(status='validation_failed', reason=str(error))
            result['attempts'].append(r)
            result['gpu_overlap'] = r
            s.persist()
            # Only confirmed/matching capacity signatures justify a smaller config.
            failure = Path(r['log']).read_text().lower()
            if r['status'] == 'ok' or not any(t in failure for t in
                ('cuda tiled initial payload launch failure', 'allocation failure', 'out of memory', 'cuda alloc')):
                break
        result['complete'] = True
        s.persist()
    if descriptor is None or poller.poll(60000):
        if descriptor is not None:
            os.close(descriptor)
        break
s.manifest['complete'] = True
s.persist()
