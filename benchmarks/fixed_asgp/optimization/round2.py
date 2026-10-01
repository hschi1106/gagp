"""Serial round-two experiments; consumes frozen inputs, never prepares them."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]
TASKS = ('sum_of_elements', 'house_robber', 'median')
FLAGS = {k: '1' for k in ('GAGP_VIEW_PROFILE', 'GAGP_TYPED_VIEW_PHASE',
    'GAGP_SORT_CASES', 'GAGP_COMPACT_FRAMES', 'GAGP_CONSTANT_PHASE',
    'GAGP_POPULATION_HANDOFF', 'GAGP_DIRECT_PHASE', 'GAGP_DIRECT_ROOT',
    'GAGP_WINDOW_EXECUTOR', 'GAGP_UNBOXED_WINDOW_FRAMES', 'GAGP_BOUND_ADD_PHASE')}
FLAGS['GAGP_CUDA_DEVICE'] = '0'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=('formal', 'quality', 'scaling'))
    parser.add_argument('out', type=Path)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--generations', type=int, default=128)
    parser.add_argument('--native-generations', type=int)
    parser.add_argument('--only', choices=('all','native','fragments'), default='all')
    parser.add_argument('--parsimony', action='store_true')
    parser.add_argument('--buckets', action='store_true', help='experimental owned capacity buckets; may regress cheap populations')
    args = parser.parse_args()
    os.chdir(ROOT)
    out = args.out.resolve(); out.mkdir(parents=True, exist_ok=False)
    binary = args.binary.resolve()
    art = ROOT / 'logs/fixed-asgp/artifacts'
    manifest = {'stage': args.stage, 'binary': str(binary), 'sha256': digest(binary),
        'head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
        'frozen': {p.name: digest(p) for p in sorted(art.glob('*.json'))}, 'cells': []}

    def run(task, mode, tag, flags, pop=1024, executable=binary):
        target = out / (tag + '.' + task + '.json')
        # The historical scaling contract takes prefixes of the frozen P8192
        # archive; no separate P2048/P4096 preparation exists or is needed.
        prepared_pop = 8192 if args.stage == 'scaling' else pop
        command = [str(executable), 'measure', str(art / (task + '.source.json')),
            str(art / f'{task}.p{prepared_pop}.prepared.json'), str(art / (task + '.grammar.json')),
            str(pop), mode, str(target)]
        env = {k: v for k, v in os.environ.items() if not k.startswith('GAGP_')}
        env.update(flags)
        started = time.monotonic()
        with target.with_suffix('.log').open('w') as log:
            result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=3600)
        cell = {'task': task, 'mode': mode, 'population': pop, 'tag': tag,
            'command': command, 'flags': flags, 'binary_sha256': digest(executable),
            'seconds': time.monotonic() - started, 'returncode': result.returncode,
            'output': str(target)}
        manifest['cells'].append(cell)
        (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(tag, task, result.returncode, round(cell['seconds'], 2), flush=True)
        if result.returncode:
            raise RuntimeError(f'failed cell: {target}')

    fragment = dict(FLAGS, GAGP_LOCAL_FRAGMENT_ADMISSION='1', GAGP_OWNED_EXECUTABLE='1')
    if args.parsimony: fragment['GAGP_FRAGMENT_PARSIMONY']='1'
    if args.buckets: fragment['GAGP_BUCKET_SMALL_PHASES']='1'
    if args.stage == 'formal':
        for process in range(3):
            for task in TASKS:
                order = ['baseline', 'native'] if process % 2 == 0 else ['native', 'baseline']
                for kind in (order if args.only != 'fragments' else []):
                    if kind == 'baseline' and args.baseline:
                        old = {k: v for k, v in FLAGS.items() if k not in ('GAGP_DIRECT_PHASE', 'GAGP_DIRECT_ROOT', 'GAGP_WINDOW_EXECUTOR', 'GAGP_UNBOXED_WINDOW_FRAMES', 'GAGP_BOUND_ADD_PHASE')}
                        run(task, 'gpu_overlap', f'baseline.p{process}', old, executable=args.baseline.resolve())
                    elif kind == 'native':
                        run(task, 'gpu_overlap', f'native.p{process}', FLAGS)
                # Each process warms a complete four-generation trajectory once,
                # then repeats it three times with fresh owners and the same seed.
                kinds = [False, True] if process % 2 == 0 else [True, False]
                for owned in ([] if args.only=='native' else [True] if args.parsimony else kinds):
                    flags = dict(fragment, GAGP_FRAGMENT_GENERATIONS='4')
                    if not owned: flags.pop('GAGP_OWNED_EXECUTABLE')
                    run(task, 'fragments_repeat', f'fragments-owned{int(owned)}.p{process}', flags)
    elif args.stage == 'quality':
        for seed in range(3):
            for task in TASKS:
                for kind in (('native', 'fragments') if args.only=='all' else (args.only,)):
                    flags = dict(FLAGS if kind == 'native' else fragment,
                        GAGP_BM_SEED=str(seed), GAGP_GPU_DIAGNOSTICS='1')
                    key = 'GAGP_SEARCH_GENERATIONS' if kind == 'native' else 'GAGP_FRAGMENT_GENERATIONS'
                    flags[key] = str(args.native_generations if kind=='native' and args.native_generations else args.generations)
                    export_key = 'GAGP_SEARCH_EXPORT' if kind == 'native' else 'GAGP_FRAGMENT_EXPORT'
                    flags[export_key] = str(out / f'{kind}.s{seed}.{task}.asts.json')
                    run(task, 'search' if kind == 'native' else 'fragments', f'{kind}.s{seed}', flags)
    else:
        for task in TASKS:
            for pop in (1024, 2048, 4096, 8192):
                for mode in ('asgp_1t', 'gagp_cpu', 'gpu_eval', 'gpu_repro', 'gpu_overlap'):
                    run(task, mode, f'{mode}.p{pop}', FLAGS, pop)
                run(task, 'fragments_repeat', f'fragments.p{pop}', dict(fragment, GAGP_FRAGMENT_GENERATIONS='4'), pop)


if __name__ == '__main__':
    main()
