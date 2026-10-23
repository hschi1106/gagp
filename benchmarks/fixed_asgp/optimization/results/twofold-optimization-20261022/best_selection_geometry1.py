"""One global candidate from complete development matrices, including admitted8 fallback."""
import time
import serial
import stage_pilot
import view_geometry_pool_pilot as stage_width_pilot
import study_statistics


def select():
    protocol=serial.read(serial.BASE/'protocol.json')
    expected={(task,p) for task in protocol['tasks'] for p in (1024,16384)}
    candidates=[]
    families=[('stage1','build-attempt19',stage_pilot.VARIANTS[1:]),('shape1','build-attempt8',('shape',))]
    for name,build_name,variants in families:
        assert serial.read(serial.RAW/(name+'-controller.json'))['status']=='requested_phase_complete'
        build=serial.read(serial.RAW/build_name/'manifest.json');assert build['status']=='ok'
        assert serial.sha(build['binary'])==build['binary_sha256']
        rows=sum([serial.read(serial.RAW/f'{name}-p{p}-comparisons.json') for p in (1024,16384)],[])
        for variant in variants:
            group=[r for r in rows if r['variant']==variant]
            assert len(group)==60 and {(r['task'],r['population']) for r in group}==expected
            assert all(all(r[k] for k in ('final_descriptors_equal','full_final_ast_equal','champion_ast_fitness_equal')) for r in group)
            means={str(p):study_statistics.geometric_mean(r['speedup'] for r in group if r['population']==p) for p in (1024,16384)}
            means['pooled']=study_statistics.geometric_mean(r['speedup'] for r in group)
            extra=stage_pilot.execution_settings(variant) if name=='stage1' else dict(stage_pilot.execution_settings('baseline'),
                    GAGP_GPU_INTEGER_CASE_REUSE='1',GAGP_GPU_SHAPE_RESULT_CACHE='1')
            candidates.append(dict(family=name,stage_family=name,variant=variant,build_name=build_name,
                geometric_means=means,binary=build['binary'],binary_sha256=build['binary_sha256'],extra=extra))
    width_controller=serial.read(serial.RAW/'view-geometry1-controller.json')
    assert width_controller['status']=='requested_phase_complete'
    geometry=width_controller['selection'];width=geometry['physical_block']
    if width or geometry['pool_mode']==0:
        rows=sum([serial.read(serial.RAW/f'view-geometry1-p{p}-comparisons.json') for p in (1024,16384)],[])
        variant=geometry['selected'];group=[r for r in rows if r['variant']==variant]
        assert len(group)==60 and {(r['task'],r['population']) for r in group}==expected
        assert all(all(r[k] for k in ('final_descriptors_equal','full_final_ast_equal','champion_ast_fitness_equal')) for r in group)
        means={str(p):study_statistics.geometric_mean(r['speedup'] for r in group if r['population']==p) for p in (1024,16384)}
        means['pooled']=study_statistics.geometric_mean(r['speedup'] for r in group)
        build=serial.read(serial.RAW/'build-attempt19/manifest.json');assert serial.sha(build['binary'])==build['binary_sha256']
        candidates.append(dict(family='view-geometry1',stage_family='view-geometry1',variant=variant,build_name='build-attempt19',
            geometric_means=means,binary=build['binary'],binary_sha256=build['binary_sha256'],
            extra=stage_width_pilot.execution_settings(variant,pool_mode=geometry['pool_mode'])))
    selected=max(candidates,key=lambda x:x['geometric_means']['pooled'])
    thread=serial.read(serial.RAW/'threads0-development-selection.json');assert thread['status']=='ok'
    return dict(scope='complete old TRAINING runtime and execution parity only; no formal/new-heldout freeze',epoch=time.time(),
        selected=selected,considered=candidates,binary=selected['binary'],binary_sha256=selected['binary_sha256'],
        rule='maximize complete60-cell geometric mean, one global setting; failed pool/elite matrices ineligible; admitted8 shape fallback included',
        thread_selection=thread,variants=dict(baseline=dict(extra=stage_pilot.execution_settings('baseline')),
            candidate=dict(extra=dict(selected['extra'],GAGP_PHASE_REPRO_THREADS=str(thread['threads'])))))
