"""One global execution setting from complete old-training speed observations."""
import time

import elite_pilot
import pool_pilot
import serial
import study_statistics


def select():
    protocol=serial.read(serial.BASE/'protocol.json')
    expected={(task,p) for task in protocol['tasks'] for p in (1024,16384)}
    candidates=[]
    families=[('pool2','build-attempt15',('before','pool','pool_reuse'),pool_pilot),
              ('elite1','build-attempt16',('pool_reuse','elite','elite_full128'),elite_pilot)]
    for name,build_name,variants,helper in families:
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
            candidates.append(dict(family=name,variant=variant,geometric_means=means,
                binary=build['binary'],binary_sha256=build['binary_sha256'],
                extra=helper.execution_settings(variant,'full',0)))
    selected=max(candidates,key=lambda x:x['geometric_means']['pooled'])
    thread=serial.read(serial.RAW/'threads0-development-selection.json');assert thread['status']=='ok'
    extra=dict(selected['extra'],GAGP_PHASE_REPRO_THREADS=str(thread['threads']))
    baseline=pool_pilot.execution_settings('baseline','full',0)
    return dict(scope='old TRAINING/runtime only; development setting, not formal/new-heldout freeze',
        epoch=time.time(),selected=selected,considered=candidates,
        rule='maximize full30/bothP pooled60 geometric mean with exact final/champion checks; one setting for all tasks',
        thread_selection=thread,
        thread_scope='global1/8/16 from separate original-binary8-task/bothP coarse pilot; combination now requires G64 training parity/runtime',
        binary=selected['binary'],binary_sha256=selected['binary_sha256'],
        variants=dict(baseline=dict(extra=baseline),candidate=dict(extra=extra)))
