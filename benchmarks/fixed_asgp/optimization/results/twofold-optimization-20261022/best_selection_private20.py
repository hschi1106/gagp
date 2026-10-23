"""Complete60-cell TRAINING selection, with admitted20 and older fallbacks."""
import time

import best_selection_geometry1
import private_launch_pilot
import serial
import study_statistics


def select():
    result=best_selection_geometry1.select()
    controller=serial.read(serial.RAW/'private-launch0-controller.json')
    assert controller['status']=='requested_phase_complete'
    variant=controller['selection']['selected']
    protocol=serial.read(serial.BASE/'protocol.json')
    expected={(task,p) for task in protocol['tasks'] for p in (1024,16384)}
    rows=sum([serial.read(serial.RAW/f'private-launch0-p{p}-comparisons.json') for p in (1024,16384)],[])
    group=[row for row in rows if row['variant']==variant]
    assert len(group)==60 and {(row['task'],row['population']) for row in group}==expected
    assert all(all(row[k] for k in ('final_descriptors_equal','full_final_ast_equal','champion_ast_fitness_equal')) for row in group)
    build=serial.read(serial.RAW/'build-attempt20/manifest.json')
    assert build['status']=='ok' and serial.sha(build['binary'])==build['binary_sha256']
    means={str(p):study_statistics.geometric_mean(row['speedup'] for row in group if row['population']==p) for p in (1024,16384)}
    means['pooled']=study_statistics.geometric_mean(row['speedup'] for row in group)
    result['considered'].append(dict(family='private-launch0',stage_family='private-launch0',variant=variant,
        build_name='build-attempt20',binary=build['binary'],binary_sha256=build['binary_sha256'],
        geometric_means=means,extra=private_launch_pilot.execution_settings(variant)))
    selected=max(result['considered'],key=lambda row:row['geometric_means']['pooled'])
    result.update(selected=selected,binary=selected['binary'],binary_sha256=selected['binary_sha256'],epoch=time.time(),
        rule='maximize complete60-cell oldTRAINING mean across admitted20/19/8; one global setting, failed matrices excluded, no heldout')
    result['variants']['candidate']['extra']=dict(selected['extra'],GAGP_PHASE_REPRO_THREADS=str(result['thread_selection']['threads']))
    return result
