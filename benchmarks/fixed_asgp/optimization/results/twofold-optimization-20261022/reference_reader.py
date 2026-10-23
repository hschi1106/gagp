"""Recompute exact CPU-reference identity and derived artifacts while reporting."""
import champion_audit_dedup as dedup
import shared_cpu_reference as shared


def reconstruct(job,evidence,settings,actual_reference=None):
    raw=evidence.output(job,'raw_output');mapping=evidence.output(job,'mapping')
    if not job.get('cpu_reference'):
        return dedup.expand_document(raw,mapping['observation_to_unique'])
    proof=job['cpu_reference'];reference=proof['reference_job']
    assert actual_reference is not None and reference==actual_reference,'CPU reference is not the actual paired origin job'
    assert reference['status']=='ok' and reference['native_fidelity_pass'] is True
    assert reference['native_cpu_executed_programs']>0 and not reference.get('cpu_reference')
    gpu=evidence.output(job,'gpu_raw_output');old=evidence.output(reference,'raw_output')
    for record in (job,reference):
        for path,digest in record['input_sha256'].items():evidence.checked(path,digest)
    source=evidence.checked(job['source'],job['input_sha256'][job['source']])
    prepared=evidence.checked(job['prepared'],job['input_sha256'][job['prepared']])
    ref_source=evidence.checked(reference['source'],reference['input_sha256'][reference['source']])
    ref_prepared=evidence.checked(reference['prepared'],reference['input_sha256'][reference['prepared']])
    same,key=shared.same_evaluation(prepared['programs'],source,prepared['grammar_hash'],shared.cpu_settings(settings,job['extra']),
        ref_prepared['programs'],ref_source,ref_prepared['grammar_hash'],shared.cpu_settings(settings,reference['extra']))
    assert same and key==proof['identity_sha256']
    assert shared.same_decode_context(prepared['programs'],source,ref_source),'source decode/literal context differs'
    assert reference['native_cpu_executed_programs']==len(ref_prepared['programs'])
    assert reference['native_cpu_executed_cases']==len(ref_prepared['programs'])*old['cases']
    merged=shared.merge(gpu,old)
    assert merged==raw,'derived CPU/GPU merge differs from original observations'
    assert job['native_cpu_executed_programs']==job['native_cpu_executed_cases']==0
    assert job['native_cpu_reference_programs']==len(prepared['programs'])
    assert job['native_cpu_reference_cases']==len(prepared['programs'])*raw['cases']
    return shared.expand(merged,mapping['observation_to_unique'])
