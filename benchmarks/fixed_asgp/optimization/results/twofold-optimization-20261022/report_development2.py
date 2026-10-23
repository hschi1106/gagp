"""Render checked development evidence after timing ends, without a fake freeze."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import time
import traceback

import development_evidence
import development_views2 as development_views
import report
import serial


def choose_reported(result, selection):
    selected = selection['selected']
    recipes = [r for r in result['recipes'] if
               r['family'] == selected['stage_family'] and r['variant'] == selected['variant']]
    if len(recipes) != 1 or not recipes[0]['complete']:
        raise ValueError('selected recipe has incomplete development evidence')
    rows = [r for r in result['cells'] if
            r['family'] == selected['stage_family'] and r['variant'] == selected['variant']]
    if {r.get('candidate_binary_sha256') for r in rows} != {selected['binary_sha256']}:
        raise ValueError('development binary differs from selected binary')
    return recipes[0]


def terminal(raw, names):
    if not names:
        raise ValueError('explicit completed timed controller names required')
    for name in names:
        data = serial.read(raw / (name + '-controller.json'))
        assert data['status'] in ('requested_phase_complete', 'stopped', 'budget_insufficient', 'ok', 'failed')
        for field in ('pid', 'child_pid'):
            path = Path(f'/proc/{data.get(field)}/stat')
            assert not path.exists() or path.read_text().split()[2] == 'Z', 'timed process still alive'


def guard_kind(kind, raw):
    if kind not in ('appendix', 'partial'):
        raise ValueError('explicit development report kind required')
    if kind == 'partial' and any((raw / ('formal0-' + scope + '-freeze.json')).exists()
                                 for scope in ('speed', 'quality')):
        raise ValueError('actual formal freeze exists; use formal report for partial formal coverage')


def main(selection_path, out, controllers, kind, regressions=None):
    assert not out.exists(), 'preserve report attempts; output must be NEW'
    out.mkdir(parents=True)
    state = dict(pid=os.getpid(), status='running', started_epoch=time.time(),
                 deadline_epoch=serial.DEADLINE, kind='development_' + kind,
                 goal_proven=False, formal_target_proven=False, independent_quality_proven=False)
    serial.save(out / 'completion.json', state)
    try:
        guard_kind(kind, serial.RAW)
        terminal(serial.RAW, controllers)
        for key in ('OMP_NUM_THREADS', 'OPENBLAS_NUM_THREADS', 'MKL_NUM_THREADS'):
            os.environ[key] = '1'
        with serial.GPU_LOCK.open('a') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            terminal(serial.RAW, controllers)
            assert time.time() < serial.DEADLINE, 'original experiment deadline reached'
            paths = [Path(__file__), serial.BASE / 'development_evidence.py',
                     serial.BASE / 'development_views2.py', serial.BASE / 'report.py',
                     selection_path, serial.BASE / 'protocol.json']
            state['source_sha256'] = {str(p): serial.sha(p) for p in paths}
            serial.save(out / 'completion.json', state)
            protocol = serial.read(serial.BASE / 'protocol.json')
            assert len(protocol['tasks']) == 30 and set(protocol['tasks']) == set(serial.PAPER_TASKS)
            selection = serial.read(selection_path)
            selected = selection['selected']
            assert serial.sha(selected['binary']) == selected['binary_sha256']
            result = development_evidence.read_families([selected['stage_family']], protocol)
            result['reported_recipe'] = choose_reported(result, selection)
            result['selection'] = selection
            result['report_kind'] = state['kind']
            result['source_regressions'] = serial.read(regressions) if regressions else dict(status='not_checked')
            if regressions:
                assert result['source_regressions']['candidate_binary_sha256'] == selected['binary_sha256'], \
                    'regression record belongs to a different candidate'
                state['source_sha256'][str(regressions)] = serial.sha(regressions)
            serial.save(out / 'summary.json', result)
            report.csv_file(out / 'development_cells.csv', result['cells'])
            report.csv_file(out / 'development_recipes.csv', result['recipes'])
            (out / 'REPORT.zh-TW.md').write_text(development_views.markdown(result))
            (out / 'dashboard.html').write_text(development_views.dashboard(result, protocol['tasks']))
            development_views.plots(result, protocol['tasks'], out)
            assert all(serial.sha(p) == digest for p, digest in state['source_sha256'].items())
            state.update(status='ok', finished_epoch=time.time(),
                within_original_deadline=time.time() <= serial.DEADLINE,
                artifact_sha256={p.name: serial.sha(p) for p in out.iterdir() if p.name != 'completion.json'})
    except BaseException as error:
        state.update(status='failed', error=repr(error), traceback=traceback.format_exc(),
                     finished_epoch=time.time())
        raise
    finally:
        serial.save(out / 'completion.json', state)


if __name__ == '__main__':
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--selection', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--controller', action='append', required=True)
    parser.add_argument('--kind', choices=['appendix', 'partial'], required=True)
    parser.add_argument('--regressions', type=Path)
    args = parser.parse_args()
    main(args.selection, args.out, args.controller, args.kind, args.regressions)
