"""Small continuation runner; consumes frozen inputs, never prepares them."""
from argparse import ArgumentParser, Namespace
from pathlib import Path
from gagp_tools.experiments.fixed_asgp.low_budget import Study, sha

p = ArgumentParser()
p.add_argument('--out', required=True, type=Path)
p.add_argument('--binary', type=Path, default=Path('cpp/build/release/gagp_fixed_asgp_bench'))
p.add_argument('--settings', type=Path, required=True)
p.add_argument('--tasks', nargs='+', default=['filter_odd', 'cumulative_sum', 'for_loop_index',
    'double_letters', 'replace_space_with_newline', 'even_squares', 'negative_to_zero',
    'fibonacci_array', 'small_or_large', 'grade'])
p.add_argument('--seeds', nargs='+', type=int, default=[0])
p.add_argument('--generations', type=int, default=4)
p.add_argument('--population', type=int, default=1024)
p.add_argument('--modes', nargs='+', default=['gpu_overlap'])
p.add_argument('--budget', type=float, default=600)
a = p.parse_args()
root = Path.cwd()
base = root / 'logs/all-task-repair-20261015'
s = Study(Namespace(out=a.out, binary=a.binary, settings=a.settings, stage='list-output-coarse',
    generations=a.generations, rss_gib=4, budget=a.budget, timeout=150,
    native_depth=24, source_balanced=True))
s.manifest['population'] = a.population
s.manifest['note'] = 'One cold search per task/seed/mode; complete cases and native pipeline. No precision or convergence-equivalence claim.'
s.manifest['controls_reused'] = {'path': str(base / 'benchmark-all-g4/manifest.json'),
    'sha256': sha(base / 'benchmark-all-g4/manifest.json'),
    'scope': 'Only P1024/C1024/G4/seed0 ratios may reuse these ASGP controls. ASGP source/settings unchanged.'}
s.persist()
for task in a.tasks:
    folder = base / 'support-balanced' / task
    prepared = base / 'benchmark-all-g4' / f'{task}.p1024.prepared.json'
    s.manifest['tasks'][task] = {'inputs': {str(f): sha(f) for f in
        [folder / 'source.json', folder / 'grammar.json', prepared]}}
    s.persist()
    for seed in a.seeds:
        for mode in a.modes:
            status = s.search(task, folder, prepared, a.population, mode, seed)
            print(task, seed, mode, status, flush=True)
