"""Select only complete global recipes, retaining every measured setting."""
import time

import best_selection_extra_recipe
import serial
import study_statistics


def select():
    previous=best_selection_extra_recipe.select()
    control=serial.read(serial.RAW/'combined3-controller.json')
    assert control['status']=='requested_phase_complete'
    candidates=list(previous['considered'])
    if control['run_full']:
        chosen=control['selection'];path=serial.RAW/'combined3-settings.json'
        assert serial.sha(path)==control['settings_sha256'] and serial.read(path)==chosen
        expected={(t,p) for t in serial.read(serial.BASE/'protocol.json')['tasks'] for p in (1024,16384)}
        rows=sum([serial.read(serial.RAW/f'combined3-p{p}-comparisons.json') for p in (1024,16384)],[])
        assert len(rows)==60 and {(r['task'],r['population']) for r in rows}==expected
        assert all(all(r[k] for k in ('final_descriptors_equal','full_final_ast_equal',
                                     'champion_ast_fitness_equal')) for r in rows)
        build=serial.read(serial.RAW/'build-attempt22/manifest.json')
        assert build['status']=='ok' and serial.sha(chosen['binary'])==chosen['binary_sha256']==build['binary_sha256']
        means={str(p):study_statistics.geometric_mean(r['speedup'] for r in rows if r['population']==p)
               for p in (1024,16384)}
        means['pooled']=study_statistics.geometric_mean(r['speedup'] for r in rows)
        candidates.append(dict(family='combined3',stage_family='combined3',variant='candidate',
                               build_name='build-attempt22',extra=chosen['extra'],geometric_means=means,
                               binary=chosen['binary'],binary_sha256=chosen['binary_sha256']))
    selected=max(candidates,key=lambda r:r['geometric_means']['pooled'])
    return dict(scope='complete60-cell oldTRAINING runtime/fidelity only; fresh heldout unopened',epoch=time.time(),
                selected=selected,considered=candidates,binary=selected['binary'],binary_sha256=selected['binary_sha256'],
                rule='maximize complete60-cell mean; keep actual global recipe and its admitted binary',
                variants=dict(baseline=previous['variants']['baseline'],candidate=dict(extra=selected['extra'])))
