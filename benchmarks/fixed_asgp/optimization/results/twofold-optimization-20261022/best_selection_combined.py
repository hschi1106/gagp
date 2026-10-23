"""Choose one complete60-cell global candidate, including composed settings."""
import time

import best_selection_private20
import projection_simple_pilot
import serial
import study_statistics


def select():
    result=best_selection_private20.select();protocol=serial.read(serial.BASE/'protocol.json')
    expected={(task,p) for task in protocol['tasks'] for p in (1024,16384)}
    for family,build_name in [('projection-simple0','build-attempt19'),('combined0','build-attempt20')]:
        control=serial.read(serial.RAW/(family+'-controller.json'));assert control['status']=='requested_phase_complete'
        variant=control['selection']['selected'] if family=='projection-simple0' else 'candidate'
        extra=control['selection']['extra']
        rows=sum([serial.read(serial.RAW/f'{family}-p{p}-comparisons.json') for p in (1024,16384)],[])
        group=[row for row in rows if row['variant']==variant]
        assert len(group)==60 and {(row['task'],row['population']) for row in group}==expected
        assert all(all(row[k] for k in ('final_descriptors_equal','full_final_ast_equal','champion_ast_fitness_equal')) for row in group)
        build=serial.read(serial.RAW/build_name/'manifest.json')
        assert build['status']=='ok' and serial.sha(build['binary'])==build['binary_sha256']
        means={str(p):study_statistics.geometric_mean(row['speedup'] for row in group if row['population']==p) for p in (1024,16384)}
        means['pooled']=study_statistics.geometric_mean(row['speedup'] for row in group)
        result['considered'].append(dict(family=family,stage_family=family,variant=variant,extra=extra,
            binary=build['binary'],binary_sha256=build['binary_sha256'],build_name=build_name,geometric_means=means))
    chosen=max(result['considered'],key=lambda row:row['geometric_means']['pooled'])
    result.update(selected=chosen,binary=chosen['binary'],binary_sha256=chosen['binary_sha256'],epoch=time.time(),
        rule='maximize complete60-cell oldTRAINING mean over composed/global component/older admitted candidates; no heldout')
    result['variants']['candidate']['extra']=dict(chosen['extra'],GAGP_PHASE_REPRO_THREADS=str(result['thread_selection']['threads']))
    return result
