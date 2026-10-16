"""One-shot G4 scaling checks; frozen inputs and existing runtime stay unchanged."""
from argparse import ArgumentParser, Namespace
from pathlib import Path
import fcntl
import json
import time

from gagp_tools.experiments.fixed_asgp.low_budget import (
    PAPER_TASKS, Study, read, save, sha, validate_search,
)

ROOT = Path.cwd()
OLD = ROOT / 'logs/all-task-repair-20261015'
POPS = (1024, 2048, 4096, 8192, 16384)


def compact(path, value):
    assert not path.exists(), f'refuse to overwrite {path}'
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, separators=(',', ':'), allow_nan=False) + '\n')
    temporary.replace(path)


def study(args):
    config = Namespace(out=args.out, binary=args.binary, settings=args.settings,
        stage='all30-scaling-' + args.stage, generations=4, rss_gib=32,
        budget=args.budget, timeout=1800, native_depth=24, source_balanced=True)
    if not args.resume:
        s = Study(config)
        provenance = args.binary.resolve().parent / 'manifest.json'
        if provenance.exists():
            m = read(provenance)
            assert m['binary_sha256'] == s.manifest['binary_sha256']
            s.manifest['binary_source_head'] = m.get('binary_source_head', m['source_head'])
        s.manifest.update(populations=list(POPS), population=None,
            note='Complete1024 cases/G4+finaleval, CPU1T/native GPUER-O, one cold process/cell. '
                 'No precision or convergence claim.16GiB preferred; memory-only retry8/4GiB disclosed.')
        s.persist()
        return s
    # Restore only this experiment's runner, never an application checkpoint.
    s = Study.__new__(Study)
    s.args = config
    s.out = args.out.resolve()
    s.manifest = read(s.out / 'manifest.json')
    assert s.manifest['stage'] == 'all30-scaling-' + args.stage
    running = {tuple(c['command']) for c in s.manifest['cells'] if c['status'] == 'running'}
    for process in Path('/proc').glob('[0-9]*'):
        try:
            command = tuple(part.decode() for part in (process / 'cmdline').read_bytes().split(b'\0') if part)
        except (OSError, UnicodeDecodeError):
            continue
        if command in running:
            raise RuntimeError(f'Previous native command still active as PID {process.name}; wait before resuming')
    s.args.binary = s.out / 'bench'
    assert sha(s.args.binary) == s.manifest['binary_sha256']
    import os
    s.env = {k: v for k, v in os.environ.items()
             if not k.startswith('GAGP_') and k not in ('LD_PRELOAD', 'MALLOC_CONF')}
    s.env.update(s.manifest['settings'])
    s.started = time.monotonic() - s.manifest.get('elapsed_s', 0)
    s.persist()
    return s


def prefix_ok(task, source, prepared=None):
    original = read(OLD / 'support-balanced' / task / 'source.json')
    assert len(source['population']) == 16384 and len(source['cases']) == 1024
    assert {k: v for k, v in source.items() if k != 'population'} == {
        k: v for k, v in original.items() if k != 'population'}
    assert source['population'][:1024] == original['population']
    if prepared is not None:
        before = read(OLD / 'benchmark-all-g4' / f'{task}.p1024.prepared.json')
        assert prepared['programs'][:1024] == before['programs']
        assert prepared['grammar_hash'] == before['grammar_hash']


def prepare(s):
    for task in PAPER_TASKS:
        record = s.manifest['tasks'].get(task)
        if record and record.get('complete'):
            for path, digest in record['hashes'].items():
                assert sha(Path(path)) == digest
            continue
        folder = s.out / task
        folder.mkdir(exist_ok=True)
        attempt = sum(c['task'] == task and c['phase'].startswith('freeze') for c in s.manifest['cells'])
        source_path = folder / f'source16384.a{attempt}.json'
        grammar = OLD / 'support-balanced' / task / 'grammar.json'
        record = s.manifest['tasks'][task] = {'complete': False, 'prefixes': {}, 'hashes': {str(grammar): sha(grammar)}}
        s.persist()
        cell = s.run(task, f'freeze.a{attempt}', [s.args.binary, 'freeze', task, source_path],
                     {'GAGP_FREEZE_POPULATION': '16384'})
        if cell['status'] != 'ok':
            record['failure'] = cell['status']; s.persist(); continue
        source = read(source_path)
        prefix_ok(task, source)
        record['hashes'][str(source_path)] = sha(source_path)
        native_path = folder / f'p16384.prepared.a{attempt}.json'
        cell = s.run(task, f'prepare16384.a{attempt}',
                     [s.args.binary, 'prepare', source_path, grammar, 16384, native_path])
        native = read(native_path) if cell['status'] == 'ok' else None
        prefix_ok(task, source, native)
        if native is not None:
            assert len(native['programs']) == 16384
        for population in POPS[1:]:
            src = source_path if population == 16384 else folder / f'p{population}.source.a{attempt}.json'
            if population != 16384:
                compact(src, dict(source, population=source['population'][:population]))
            dst = native_path if population == 16384 else folder / f'p{population}.prepared.a{attempt}.json'
            if native is not None and population != 16384:
                sliced = dict(native, programs=native['programs'][:population])
                sliced['source_depth_audit'] = native.get('source_depth_audit', [])[:population]
                compact(dst, sliced)
            elif native is None and population != 16384:
                smaller = s.run(task, f'prepare{population}.a{attempt}',
                    [s.args.binary, 'prepare', src, grammar, population, dst])
                if smaller['status'] != 'ok':
                    record['prefixes'][str(population)] = {'source': str(src), 'grammar': str(grammar),
                        'prepared': None, 'status': 'admission_' + smaller['status']}
                    record['hashes'][str(src)] = sha(src)
                    continue
            ok = dst.exists()
            record['prefixes'][str(population)] = {'source': str(src), 'grammar': str(grammar),
                'prepared': str(dst) if ok else None, 'status': 'ok' if ok else 'admission_' + cell['status']}
            for path in [src] + ([dst] if ok else []):
                record['hashes'][str(path)] = sha(path)
        record['complete'] = True
        s.persist()
        print('prepared', task, {p: r['status'] for p, r in record['prefixes'].items()}, flush=True)


def measure(s, artifacts):
    inputs = read(artifacts / 'manifest.json')
    for task, record in inputs['tasks'].items():
        for path, digest in record['hashes'].items():
            assert sha(Path(path)) == digest
    s.manifest['artifacts_manifest'] = {'path': str(artifacts / 'manifest.json'),
                                      'sha256': sha(artifacts / 'manifest.json')}
    s.manifest.setdefault('results', {})
    s.persist()
    for population in POPS:
        for task in PAPER_TASKS:
            key = f'{task}.p{population}'
            if s.manifest['results'].get(key, {}).get('complete'):
                continue
            entry = ({'source': str(OLD / 'support-balanced' / task / 'source.json'),
                      'grammar': str(OLD / 'support-balanced' / task / 'grammar.json'),
                      'prepared': str(OLD / 'benchmark-all-g4' / f'{task}.p1024.prepared.json')}
                     if population == 1024 else
                     inputs['tasks'].get(task, {}).get('prefixes', {}).get(str(population)))
            if not entry:
                s.manifest['results'][key] = {'complete': True, 'status': 'missing_artifacts'}
                s.persist(); continue
            record = s.manifest['results'].setdefault(key, {'complete': False, 'attempts': []})
            record['inputs'] = {path: sha(Path(path)) for path in entry.values()
                                if path and Path(path).is_file()}
            for mode in ['asgp_1t', 'gpu_overlap']:
                if mode in record:
                    continue
                if population == 1024 and mode == 'gpu_overlap':
                    previous = ROOT / 'logs/all30-scaling-20261016/p1024'
                    prior = read(previous / 'manifest.json')
                    if prior['binary_sha256'] == s.manifest['binary_sha256'] and prior['settings'] == s.manifest['settings']:
                        assert prior['tasks'][task]['inputs'] == record['inputs']
                        f = previous / f'{task}.gpu_overlap.s0.json'
                        d = read(f)
                        validate_search(d, population, 4, mode)
                        record[mode] = {'status': 'ok', 'workspace_mib': 16384,
                            'output': str(f), 'sha256': sha(f), 'reused_current_binary_run': True,
                            'evolve_wall_ms': d['evolve_wall_ms'], 'final_best': d['final_best'],
                            'cpu_audit_mismatches': d['cpu_audit_mismatches']}
                        s.persist()
                        continue
                if mode == 'gpu_overlap' and entry['prepared'] is None:
                    record[mode] = {'status': entry['status']}; s.persist(); continue
                caps = [16384, 8192, 4096] if mode == 'gpu_overlap' else [16384]
                for cap in caps:
                    prefix = f'p{population}.{mode}.'
                    attempt = sum(c['task'] == task and c['phase'].startswith(prefix)
                                  for c in s.manifest['cells'])
                    tag = f'p{population}.{mode}.w{cap}.a{attempt}'
                    output, export = s.out / f'{task}.{tag}.json', s.out / f'{task}.{tag}.final.json'
                    cell = s.run(task, tag, [s.args.binary, 'measure', entry['source'],
                        entry['prepared'] or '/unused', entry['grammar'], population, 'search_' + mode, output],
                        {'GAGP_GPU_PAYLOAD_WORKSPACE_MIB': str(cap), 'GAGP_SEARCH_GENERATIONS': '4',
                         'GAGP_BM_SEED': '0', 'GAGP_SEARCH_EXPORT': str(export)})
                    result = {'status': cell['status'], 'workspace_mib': cap if mode == 'gpu_overlap' else None, 'output': str(output),
                              'process_s': cell.get('process_s'), 'log': cell.get('log')}
                    if cell['status'] == 'ok':
                        try:
                            d = read(output)
                            validate_search(d, population, 4, mode)
                            result.update(evolve_wall_ms=d['evolve_wall_ms'], final_best=d['final_best'],
                                cpu_audit_mismatches=d.get('cpu_audit_mismatches'))
                        except (ValueError, KeyError) as error:
                            result.update(status='validation_failed', reason=str(error))
                    record['attempts'].append(dict(mode=mode, **result))
                    # Retry only allocation failures; no semantic-error retries.
                    log = (s.out / cell['log']).read_text() if cell.get('log') else ''
                    memory_failure = cell['status'] == 'failed' and any(text in log.lower() for text in
                        ['allocation failure', 'out of memory', 'cuda alloc', 'cudamalloc', 'bad_alloc'])
                    if result['status'] == 'ok' or not memory_failure or cap == caps[-1]:
                        record[mode] = result
                        break
                    s.persist()
                s.persist()
            record['complete'] = True
            a, g = record['asgp_1t'], record['gpu_overlap']
            record['status'] = 'pass' if a['status'] == g['status'] == 'ok' else 'failed'
            record['speedup'] = a['evolve_wall_ms'] / g['evolve_wall_ms'] if record['status'] == 'pass' else None
            s.persist()
            print(key, record['status'], record['speedup'], flush=True)


parser = ArgumentParser(description=__doc__)
parser.add_argument('stage', choices=['prepare', 'measure'])
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--artifacts', type=Path)
parser.add_argument('--resume', action='store_true')
parser.add_argument('--budget', type=float, default=43200)
parser.add_argument('--binary', type=Path, default=Path('logs/list-5x-20261015/pooled16-ten/bench'))
parser.add_argument('--settings', type=Path, default=Path('benchmarks/fixed_asgp/optimization/results/list-5x-20261015/settings-workspace-pool16.json'))
args = parser.parse_args()
args.out.parent.mkdir(parents=True, exist_ok=True)
controller_lock = (args.out.parent / (args.out.name + '.controller.lock')).open('a')
fcntl.flock(controller_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
s = study(args)
s.manifest.setdefault('runner_versions', []).append({'path': str(Path(__file__).resolve()),
                                                   'sha256': sha(Path(__file__))})
s.persist()
if args.stage == 'prepare':
    prepare(s)
elif args.artifacts:
    measure(s, args.artifacts.resolve())
else:
    parser.error('--artifacts required for measure')
