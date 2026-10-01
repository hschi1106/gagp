"""Serial native GPU evolution experiments; consumes frozen inputs, never prepares them."""
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
    'GAGP_WINDOW_EXECUTOR', 'GAGP_UNBOXED_WINDOW_FRAMES', 'GAGP_BOUND_ADD_PHASE',
    'GAGP_REUSE_ADMISSION_COMPILE', 'GAGP_REUSE_SITE_CONTRACTS', 'GAGP_SELECTED_SITES')}
FLAGS['GAGP_CUDA_DEVICE'] = '0'
FLAGS['GAGP_HOST_THREADS'] = '1'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=('formal', 'quality', 'scaling'))
    parser.add_argument('out', type=Path)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--generations', type=int, default=128)
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

    if args.stage == 'formal':
        for process in range(3):
            for task in TASKS:
                order = ['baseline', 'native'] if process % 2 == 0 else ['native', 'baseline']
                for kind in order:
                    if kind == 'baseline' and args.baseline:
                        old = {k: v for k, v in FLAGS.items() if k not in ('GAGP_DIRECT_PHASE', 'GAGP_DIRECT_ROOT', 'GAGP_WINDOW_EXECUTOR', 'GAGP_UNBOXED_WINDOW_FRAMES', 'GAGP_BOUND_ADD_PHASE',
    'GAGP_REUSE_ADMISSION_COMPILE', 'GAGP_REUSE_SITE_CONTRACTS', 'GAGP_SELECTED_SITES')}
                        run(task, 'gpu_overlap', f'baseline.p{process}', old, executable=args.baseline.resolve())
                    elif kind == 'native':
                        run(task, 'gpu_overlap', f'native.p{process}', FLAGS)
    elif args.stage == 'quality':
        for seed in range(3):
            for task in TASKS:
                flags = dict(FLAGS, GAGP_BM_SEED=str(seed), GAGP_GPU_DIAGNOSTICS='1',
                    GAGP_SEARCH_GENERATIONS=str(args.generations),
                    GAGP_SEARCH_EXPORT=str(out / f'native.s{seed}.{task}.asts.json'))
                run(task, 'search', f'native.s{seed}', flags)
    else:
        for task in TASKS:
            for pop in (1024, 2048, 4096, 8192):
                for mode in ('asgp_1t', 'gagp_cpu', 'gpu_eval', 'gpu_repro', 'gpu_overlap'):
                    run(task, mode, f'{mode}.p{pop}', FLAGS, pop)


if __name__ == '__main__':
    main()
