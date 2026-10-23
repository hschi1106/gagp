"""Account for Python-side CPU work as well as actual native child wall time."""
import time

import full_population_audit_shared as admitted

validate=admitted.validate


def run(stage,search_job,inputs,freeze,reference=None):
    cpu=time.process_time();wall=time.time()
    result=admitted.run(stage,search_job,inputs,freeze,reference)
    result.update(native_controller_cpu_s=time.process_time()-cpu,
        native_call_elapsed_s_including_lock_wait=time.time()-wall)
    stage.persist()
    return result
