"""Run the exported winning preset once, under the same GPU exclusion lock.

This is an explicit new user invocation. It never creates a formal sample,
selects a candidate, retries a failure, or overwrites an existing artifact.
"""
from argparse import ArgumentParser
import fcntl
import json
import os
from pathlib import Path
import subprocess
import time

import serial
import winner_preset


def execute_once(command, environment, outputs, hashes, preset):
    """Retain the first failed spawn/exit; never convert it into a retry."""
    if any(path.exists() for path in outputs):
        raise ValueError('preserve existing invocation artifacts')
    out = outputs[0]
    out.parent.mkdir(parents=True, exist_ok=True)
    started = time.time()
    returncode = 127
    status = 'spawn_failed'
    error = None
    with out.with_suffix('.log').open('x') as log:
        try:
            result = subprocess.run(command, cwd=serial.ROOT, env=environment,
                                    stdout=log, stderr=subprocess.STDOUT)
            returncode = result.returncode
            status = 'process_exited'
        except OSError as exception:
            error = repr(exception)
            log.write(error + '\n')
    receipt = dict(
        status=status, returncode=returncode, error=error,
        started_epoch=started, finished_epoch=time.time(), command=command,
        binary_sha256=preset['binary_sha256'], input_sha256=hashes,
        settings={k: v for k, v in environment.items()
                  if k.startswith('GAGP_') or k in ('LD_PRELOAD', 'MALLOC_CONF')},
        artifacts={str(p): serial.sha(p) for p in outputs[:-1] if p.exists()},
        scope='new explicit user invocation; not formal experiment evidence; completion does not imply solved task or CPU parity')
    with out.with_suffix('.receipt.json').open('x') as stream:
        json.dump(receipt, stream, indent=2)
        stream.write('\n')
    print('Exit', returncode, '; output', out, '; receipt', out.with_suffix('.receipt.json'))
    return returncode


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--preset', type=Path, default=serial.BASE / 'final/winner_preset.json')
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--prepared', type=Path, required=True)
    parser.add_argument('--grammar', type=Path, required=True)
    parser.add_argument('--population', type=int, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--generations', type=int)
    parser.add_argument('--cases', type=int)
    parser.add_argument('--seed', type=int, default=0)
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    preset = serial.read(args.preset)
    command, environment = winner_preset.invocation(
        preset, serial.ROOT, args.source, args.prepared, args.grammar,
        args.population, args.out, args.generations, args.cases, args.seed, os.environ)
    outputs = [args.out, args.out.with_suffix('.population.json'), args.out.with_suffix('.history.json'),
               args.out.with_suffix('.log'), args.out.with_suffix('.receipt.json')]
    if len(set(outputs)) != len(outputs):
        raise ValueError('output paths collide; use a new .json filename')
    if args.dry_run:
        print(json.dumps(dict(command=command, settings={
            k: v for k, v in environment.items()
            if k.startswith('GAGP_') or k in ('LD_PRELOAD', 'MALLOC_CONF')},
            gpu_lock=preset['gpu_lock']), indent=2))
        return
    with Path(preset['gpu_lock']).open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        for path in outputs:
            if path.exists():
                raise ValueError('preserve existing output: ' + str(path))
        if serial.sha(command[0]) != preset['binary_sha256']:
            raise ValueError('pinned winner binary changed')
        hashes = {str(p): serial.sha(p) for p in [args.preset, args.source, args.prepared, args.grammar]}
        return execute_once(command, environment, outputs, hashes, preset)


if __name__ == '__main__':
    raise SystemExit(main())
