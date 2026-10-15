"""Bounded support/quality checks and one-shot native optimization feedback.

Never a precision benchmark. New output directories only; full 1024 cases.
Run as python -m gagp_tools.experiments.fixed_asgp.low_budget.
"""
from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import shutil
import subprocess
import time

from .grammar import definition
from .runner import ROOT, MODES

PAPER_TASKS = (
    'filter_odd', 'sum_of_elements', 'is_sorted', 'index_of_element', 'all_equal',
    'cumulative_sum', 'fuel_cost', 'luhn', 'min', 'mpi', 'count_odds',
    'last_index_of_zero', 'small_or_large', 'for_loop_index', 'double_letters',
    'replace_space_with_newline', 'even_squares', 'sum_of_squares',
    'negative_to_zero', 'scrabble_score', 'grade', 'median', 'smallest',
    'syllables', 'fibonacci', 'tribonacci', 'house_robber', 'subregion_sum',
    'fibonacci_array', 'stock_with_cooldown',
)
LOCK = '/tmp/gagp-support-20261015-gpu.lock'


def save(path: Path, value) -> None:
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')
    temporary.replace(path)


def read(path: Path):
    return json.loads(path.read_text())


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def process_group_rss(group: int) -> int:
    rss = 0
    for path in Path('/proc').glob('[0-9]*/stat'):
        try:
            fields = path.read_text().rsplit(')', 1)[1].split()
            if int(fields[2]) == group:
                rss += int(fields[21]) * os.sysconf('SC_PAGE_SIZE')
        except (OSError, ValueError, IndexError):
            pass
    return rss


def validate_search(result: dict, population: int, generations: int, mode: str) -> None:
    if result['population'] != population or result['cases'] != 1024:
        raise ValueError('search dimensions differ from declared contract')
    rows = result['generations']
    if len(rows) != generations or len(result['final_population']) != population:
        raise ValueError('incomplete generations/population')
    if not math.isfinite(result['final_best']):
        raise ValueError('non-finite fitness')
    if any(row['expected_program_cases'] != population * 1024 for row in rows):
        raise ValueError('incomplete evaluation workload')
    if result.get('cpu_audit_mismatches', 0):
        raise ValueError('independent CPU final-program fitness mismatch')
    if mode in ('gpu_repro', 'gpu_overlap'):
        if result.get('reproduction_profile') == 'cpu':
            raise ValueError('CPU reproduction cannot qualify as GPU reproduction')
        if sum(row.get('repro_kernel_ms', 0) for row in rows) <= 0:
            raise ValueError('no GPU variation kernel recorded')


class Study:
    def __init__(self, args):
        self.args = args
        self.out = args.out.resolve()
        self.out.mkdir(parents=True, exist_ok=False)
        self.started = time.monotonic()
        original_binary = args.binary.resolve()
        args.binary = self.out / "bench"
        shutil.copy2(original_binary, args.binary)
        flags = read(args.settings)
        flags.pop('GAGP_BM_COMMON_FITNESS', None)
        flags.update(GAGP_HOST_THREADS='1', OMP_NUM_THREADS='1',
                     OPENBLAS_NUM_THREADS='1', MKL_NUM_THREADS='1',
                     GAGP_BM_ALIGNED_FITNESS='1', GAGP_FREEZE_POPULATION='1024')
        self.env = {k: v for k, v in os.environ.items()
                    if not k.startswith('GAGP_') and k not in ('LD_PRELOAD', 'MALLOC_CONF')}
        self.env.update(flags)
        self.manifest = {
            'format': 'asgp-low-budget-v1', 'stage': args.stage,
            'purpose': 'support and coarse optimization feedback; not precision speedup or convergence evidence',
            'population': 8 if args.stage == 'support' else 1024, 'cases': 1024,
            'generations': 2, 'host_threads': 1, 'wall_budget_s': args.budget,
            'binary': str(args.binary.resolve()), 'binary_sha256': sha(args.binary),
            'build_binary': str(original_binary),
            'source_head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
            'settings': flags, 'tasks': {}, 'cells': [],
        }
        self.persist()

    def persist(self):
        self.manifest['elapsed_s'] = time.monotonic() - self.started
        save(self.out / 'manifest.json', self.manifest)

    def run(self, task: str, phase: str, command: list, extra=None):
        tag = task + '.' + phase
        remaining = self.args.budget - (time.monotonic() - self.started)
        cell = {'task': task, 'phase': phase, 'command': list(map(str, command)),
                'status': 'budget_not_run' if remaining <= 0 else 'waiting'}
        self.manifest['cells'].append(cell)
        self.persist()
        if remaining <= 0:
            return cell
        env = dict(self.env)
        env.update(extra or {})
        cell['extra_settings'] = extra or {}
        # flock is nonblocking so waiting for another process also obeys budget.
        with open(LOCK, 'a') as lock:
            while True:
                try:
                    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    break
                except BlockingIOError:
                    if time.monotonic() - self.started >= self.args.budget:
                        cell['status'] = 'budget_not_run'
                        self.persist()
                        return cell
                    time.sleep(1)
            remaining = self.args.budget - (time.monotonic() - self.started)
            if remaining <= 0:
                cell['status'] = 'budget_not_run'
                self.persist()
                return cell
            start = time.monotonic()
            deadline = start + min(self.args.timeout, remaining)
            cell.update(status='running', log=tag + '.log')
            self.persist()
            with (self.out / (tag + '.log')).open('w') as log:
                process = subprocess.Popen(cell['command'], cwd=ROOT, env=env,
                    stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
                peak = 0
                reason = None
                while process.poll() is None:
                    peak = max(peak, process_group_rss(process.pid))
                    if peak > self.args.rss_gib * 1024**3:
                        reason = 'memory_limit'
                    elif time.monotonic() >= deadline:
                        reason = 'timeout'
                    if reason:
                        os.killpg(process.pid, signal.SIGTERM)
                        try:
                            process.wait(timeout=3)
                        except subprocess.TimeoutExpired:
                            os.killpg(process.pid, signal.SIGKILL)
                            process.wait()
                        break
                    time.sleep(.25)
                cell.update(status=reason or ('ok' if process.returncode == 0 else 'failed'),
                            returncode=process.returncode, peak_rss_bytes=peak,
                            process_s=time.monotonic() - start)
        self.persist()
        print(task, phase, cell['status'], round(cell.get('process_s', 0), 3), flush=True)
        return cell

    def support(self, task):
        folder = self.out / task
        folder.mkdir()
        record = self.manifest['tasks'][task] = {'status': 'pending'}
        contract = folder / 'contract.json'
        source = folder / 'source.json'
        grammar = folder / 'grammar.json'
        prepared = folder / 'p8.prepared.json'
        for phase, command in (
            ('contract', ['contract', task, contract]),
            ('freeze', ['freeze', task, source]),
        ):
            cell = self.run(task, phase, [self.args.binary, *command])
            if cell['status'] != 'ok':
                record.update(status=cell['status'], blocked_at=phase)
                return
        try:
            save(grammar, definition(ROOT, task, contract=read(contract)))
        except (ValueError, KeyError, NotImplementedError) as error:
            record.update(status='unsupported', blocked_at='grammar', reason=str(error))
            return
        for phase, command in (
            ('prepare', ['prepare', source, grammar, 8, prepared]),
            ('audit', ['audit', source, grammar, folder / 'audit.json']),
        ):
            cell = self.run(task, phase, [self.args.binary, *command])
            if cell['status'] != 'ok':
                record.update(status=cell['status'], blocked_at=phase)
                return
        record['artifacts'] = {p.name: sha(p) for p in folder.glob('*.json')}
        record['status'] = 'audit_pass'
        statuses = []
        for mode in MODES:
            statuses.append(self.search(task, folder, prepared, 8, mode, 0))
        record['status'] = 'smoke_pass' if all(s == 'ok' for s in statuses) else 'smoke_failed'

    def search(self, task, artifacts, prepared, population, mode, seed):
        tag = f'{mode}.s{seed}'
        output = self.out / f'{task}.{tag}.json'
        export = self.out / f'{task}.{tag}.final.json'
        cell = self.run(task, tag, [self.args.binary, 'measure', artifacts / 'source.json',
            prepared, artifacts / 'grammar.json', population, 'search_' + mode, output],
            {'GAGP_SEARCH_GENERATIONS': '2', 'GAGP_BM_SEED': str(seed),
             'GAGP_SEARCH_EXPORT': str(export)})
        if cell['status'] == 'ok':
            try:
                result = read(output)
                validate_search(result, population, 2, mode)
                cell.update(evolve_wall_ms=result['evolve_wall_ms'], final_best=result['final_best'],
                            output=output.name, unique_final_genomes=result['unique_final_genomes'])
            except (ValueError, KeyError) as error:
                cell.update(status='validation_failed', reason=str(error))
        self.persist()
        return cell['status']

    def larger(self, task):
        prior = read(self.args.artifacts / 'manifest.json')
        record = self.manifest['tasks'][task] = {'status': 'pending'}
        old = prior['tasks'].get(task, {})
        if old.get('status') != 'smoke_pass':
            record.update(status='not_qualified', reason='all-mode small-population support gate not passed')
            return
        artifacts = self.args.artifacts / task
        for name, digest in old['artifacts'].items():
            if sha(artifacts / name) != digest:
                raise ValueError('frozen artifact changed: ' + str(artifacts / name))
        prepared = self.out / (task + '.p1024.prepared.json')
        cell = self.run(task, 'prepare1024', [self.args.binary, 'prepare', artifacts / 'source.json',
            artifacts / 'grammar.json', 1024, prepared])
        if cell['status'] != 'ok':
            record.update(status=cell['status'], blocked_at='prepare1024')
            return
        statuses = []
        seeds = (0, 1, 2) if self.args.stage == 'quality' else (0,)
        for seed in seeds:
            for mode in MODES:
                status = self.search(task, artifacts, prepared, 1024, mode, seed)
                statuses.append(status)
                if status == 'ok' and self.args.stage == 'quality':
                    tag = f'{mode}.s{seed}'
                    audit = self.run(task, tag + '.heldout', [self.args.binary, 'quality', task,
                        self.out / f'{task}.{tag}.final.json', artifacts / 'grammar.json',
                        self.out / f'{task}.{tag}.heldout.json'])
                    statuses.append(audit['status'])
        record['status'] = 'passed' if all(s == 'ok' for s in statuses) else 'incomplete_or_failed'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=('support', 'quality', 'benchmark'))
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--binary', type=Path, default=ROOT / 'cpp/build/release/gagp_fixed_asgp_bench')
    parser.add_argument('--settings', type=Path, default=ROOT / 'benchmarks/fixed_asgp/optimization/results/expanded-20261014/settings.json')
    parser.add_argument('--artifacts', type=Path)
    parser.add_argument('--tasks', nargs='+', choices=PAPER_TASKS)
    parser.add_argument('--budget', type=float, default=600)
    parser.add_argument('--timeout', type=float, default=30)
    parser.add_argument('--rss-gib', type=float, default=4)
    args = parser.parse_args()
    if args.budget <= 0 or args.timeout <= 0 or args.rss_gib <= 0:
        parser.error('budgets must be positive')
    if args.stage != 'support' and not args.artifacts:
        parser.error('--artifacts support output is required')
    args.binary = args.binary.resolve()
    study = Study(args)
    tasks = args.tasks or (PAPER_TASKS if args.stage == 'support' else ('sum_of_elements', 'house_robber', 'median'))
    try:
        for task in tasks:
            (study.support if args.stage == 'support' else study.larger)(task)
            study.persist()
    finally:
        study.persist()
    # All requested rows remain explicit, including budget-skipped tasks.
    return 0 if all(v['status'] in ('smoke_pass', 'passed') for v in study.manifest['tasks'].values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
