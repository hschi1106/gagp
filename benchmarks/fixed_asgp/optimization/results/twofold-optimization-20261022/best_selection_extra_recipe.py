"""Full60-cell candidate selection including admitted global block/cache recipe."""
import time

import best_selection_coarse_recipe
import serial
import study_statistics


def select():
    original=best_selection_coarse_recipe.select()
    control=serial.read(serial.RAW/'combined2-controller.json')
    assert control['status']=='requested_phase_complete'
    candidates=list(original['considered'])
    if control['run_full']:
        protocol=serial.read(serial.BASE/'protocol.json')
        expected={(t,p) for t in protocol['tasks'] for p in (1024,16384)}
        rows=sum([serial.read(serial.RAW/f'combined2-p{p}-comparisons.json') for p in (1024,16384)],[])
        assert len(rows)==60 and {(r['task'],r['population']) for r in rows}==expected
        assert all(all(r[k] for k in ('final_descriptors_equal','full_final_ast_equal',
                                     'champion_ast_fitness_equal')) for r in rows)
        chosen=control['selection']
        settings=serial.RAW/'combined2-settings.json'
        assert serial.sha(settings)==control['settings_sha256'] and serial.read(settings)==chosen
        build=serial.read(serial.RAW/chosen['build_name']/'manifest.json')
        assert build['status']=='ok' and serial.sha(chosen['binary'])==chosen['binary_sha256']==build['binary_sha256']
        means={str(p):study_statistics.geometric_mean(r['speedup'] for r in rows if r['population']==p)
               for p in (1024,16384)}
        means['pooled']=study_statistics.geometric_mean(r['speedup'] for r in rows)
        candidates.append(dict(family='combined2',stage_family='combined2',variant='candidate',
                               build_name=chosen['build_name'],extra=chosen['extra'],geometric_means=means,
                               binary=chosen['binary'],binary_sha256=chosen['binary_sha256']))
    selected=max(candidates,key=lambda r:r['geometric_means']['pooled'])
    return dict(scope='complete60-cell oldTRAINING runtime/fidelity only; no fresh heldout',epoch=time.time(),
                selected=selected,considered=candidates,binary=selected['binary'],binary_sha256=selected['binary_sha256'],
                rule='maximize complete60-cell mean; exact measured recipe retained; no block/cache on older fallback',
                variants=dict(baseline=original['variants']['baseline'],candidate=dict(extra=selected['extra'])))
