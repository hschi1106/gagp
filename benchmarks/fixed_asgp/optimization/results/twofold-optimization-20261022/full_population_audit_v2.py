"""Budget guard around the admitted full-population native audit body."""
import time
import full_population_audit as admitted
import serial

validate=admitted.validate


def run(stage,search_job,inputs,freeze):
    # Failed searches retain their original failure classification. A successful
    # search can lack budget for offline auditing; never open its large AST/source
    # artifacts once the reserve cutoff has already been reached.
    if search_job['status']=='ok' and time.time()>=serial.DEADLINE-freeze['reserve_s']:
        tag=f'{search_job["task"]}.p{search_job["population"]}.{search_job["variant"]}.final.full-native'
        assert tag not in stage.m['jobs']
        row=dict(status='budget_not_run',inputs_not_read=True,task=search_job['task'],
                 population=search_job['population'],variant=search_job['variant'],seed=search_job['seed'],
                 repetition=search_job['repetition'],diagnostic='all final members, exact AST dedup, native CPU/GPU fitness and wrong',
                 reason='offline full native audit reserve cutoff before opening exported populations')
        stage.m['jobs'][tag]=row;stage.persist();return row
    return admitted.run(stage,search_job,inputs,freeze)
