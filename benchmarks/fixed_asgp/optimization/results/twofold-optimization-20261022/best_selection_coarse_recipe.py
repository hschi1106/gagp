"""Full60-cell selection after coarse composition; unfinished ablations excluded."""
import time

import serial
import stage_pilot
import study_statistics


def select():
    protocol=serial.read(serial.BASE/'protocol.json')
    expected={(task,p) for task in protocol['tasks'] for p in (1024,16384)}
    options=[('stage1','build-attempt19',v,stage_pilot.execution_settings(v)) for v in stage_pilot.VARIANTS[1:]]
    options.append(('shape1','build-attempt8','shape',dict(stage_pilot.execution_settings('baseline'),
        GAGP_GPU_INTEGER_CASE_REUSE='1',GAGP_GPU_SHAPE_RESULT_CACHE='1')))
    composed=serial.read(serial.RAW/'combined1-controller.json');assert composed['status']=='requested_phase_complete'
    options.append(('combined1','build-attempt20','candidate',composed['selection']['extra']))
    # If an individual followup started before the gate, retain its eligible
    # complete result too. Coarse-only/partial/failed means are never eligible.
    for family,build_name in [('private-launch0','build-attempt20'),('projection-simple0','build-attempt19'),('view-geometry1','build-attempt19')]:
        control=serial.read(serial.RAW/(family+'-controller.json'))
        if control['status']!='requested_phase_complete':continue
        if not all((serial.RAW/f'{family}-p{p}-comparisons.json').exists() for p in (1024,16384)):continue
        choice=control['selection'];variant=choice['selected']
        if family=='view-geometry1':
            import view_geometry_pool_pilot
            extra=view_geometry_pool_pilot.execution_settings(variant,pool_mode=choice['pool_mode'])
        else:extra=choice['extra']
        options.append((family,build_name,variant,extra))
    candidates=[]
    for family,build_name,variant,extra in options:
        assert serial.read(serial.RAW/(family+'-controller.json'))['status']=='requested_phase_complete'
        rows=sum([serial.read(serial.RAW/f'{family}-p{p}-comparisons.json') for p in (1024,16384)],[])
        group=[row for row in rows if row['variant']==variant]
        assert len(group)==60 and {(row['task'],row['population']) for row in group}==expected
        assert all(all(row[k] for k in ('final_descriptors_equal','full_final_ast_equal','champion_ast_fitness_equal')) for row in group)
        means={str(p):study_statistics.geometric_mean(row['speedup'] for row in group if row['population']==p) for p in (1024,16384)}
        means['pooled']=study_statistics.geometric_mean(row['speedup'] for row in group)
        build=serial.read(serial.RAW/build_name/'manifest.json')
        assert build['status']=='ok' and serial.sha(build['binary'])==build['binary_sha256']
        candidates.append(dict(family=family,stage_family=family,variant=variant,build_name=build_name,
            extra=extra,geometric_means=means,binary=build['binary'],binary_sha256=build['binary_sha256']))
    selected=max(candidates,key=lambda row:row['geometric_means']['pooled'])
    thread=serial.read(serial.RAW/'threads0-development-selection.json');assert thread['status']=='ok'
    return dict(scope='complete60-cell oldTRAINING runtime/fidelity only; no fresh heldout',epoch=time.time(),
        selected=selected,considered=candidates,binary=selected['binary'],binary_sha256=selected['binary_sha256'],
        rule='maximize complete60-cell mean; composed candidate/older admitted fallback/completed individual only; partial ablations excluded',
        thread_selection=thread,variants=dict(baseline=dict(extra=stage_pilot.execution_settings('baseline')),
            candidate=dict(extra=dict(selected['extra'],GAGP_PHASE_REPRO_THREADS=str(thread['threads'])))))
