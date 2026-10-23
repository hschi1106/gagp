"""Exact paired CPU reference reuse; candidate GPU observations remain fresh.

Identity is complete serialized ASTs, ordered cases/labels, grammar and effective
CPU settings, not a digest comparison. Shared references execute zero new CPU
programs; their original actual CPU executions remain counted at the origin.
"""
import hashlib
import json

import champion_audit_dedup as dedup

CPU_FIELDS=('cpu_fitness','cpu_wrong','cpu_cases','cpu_errors','cpu_timeouts')


def canonical(value):
    return json.dumps(value,sort_keys=True,separators=(',',':'),allow_nan=False)


def cpu_settings(settings,extra):
    effective=dict(settings,**extra)
    return {key:value for key,value in effective.items()
            if not key.startswith('GAGP_GPU_') and key!='GAGP_SNAPSHOT_CPU'}


def same_evaluation(programs,source,grammar,settings,ref_programs,ref_source,ref_grammar,ref_settings):
    a=dict(programs=programs,cases=source['cases'],task=source['task'],grammar=grammar,settings=settings,
           fuel=2000000,reduction_lanes=512)
    b=dict(programs=ref_programs,cases=ref_source['cases'],task=ref_source['task'],grammar=ref_grammar,settings=ref_settings,
           fuel=2000000,reduction_lanes=512)
    return canonical(a)==canonical(b),hashlib.sha256(canonical(a).encode()).hexdigest()


def same_decode_context(programs,source,reference_source):
    # Parent ASTs are decode-only, but they may also seed immutable literal
    # payload registries. Keep their complete serialized prefix in the proof.
    size=len(programs)
    if len(source.get('population',[]))<size or len(reference_source.get('population',[]))<size:return False
    def context(original):
        return {key:(value[:size] if key=='population' else value) for key,value in original.items()
                if key!='diagnostic_source_parents'}
    return canonical(context(source))==canonical(context(reference_source))


def merge(gpu,reference):
    assert gpu['task']==reference['task'] and gpu['cases']==reference['cases']
    assert gpu['population']==reference['population']==len(gpu['programs'])==len(reference['programs'])>0
    assert gpu['diagnostic_owner'].startswith('native-gpu-phase-')
    count=gpu['cases'];rows=[]
    for index,(current,old) in enumerate(zip(gpu['programs'],reference['programs'])):
        assert current['program']==old['program']==index
        assert current['nodes']==old['nodes'] and current['cases']==old['cases']==old['cpu_cases']==count
        assert not any(key.startswith('cpu_') for key in current),'GPU-only snapshot unexpectedly ran CPU'
        assert all(key in old for key in CPU_FIELDS)
        rows.append(dict(current,**{key:old[key] for key in CPU_FIELDS}))
    return dict(gpu,programs=rows)


def expand(document,mapping):
    expanded=dedup.expand_document(document,mapping)
    unique=document['population'];count=document['cases']
    expanded['deduplication'].update(rule='exact within-call AST dedup plus exact paired CPU reference; candidate GPU reexecuted',
        native_cpu_executed_programs=0,native_cpu_executed_cases=0,
        native_cpu_reference_programs=unique,native_cpu_reference_cases=unique*count)
    return expanded


def coverage_valid(row,population,count):
    executed=row.get('native_cpu_executed_programs',0)
    if executed>0:return executed<=population and row.get('native_cpu_executed_cases')==executed*count
    reference=row.get('native_cpu_reference_programs',0)
    return (executed==0 and row.get('native_cpu_executed_cases')==0 and row.get('cpu_reference_verified') is True
            and 0<reference<=population and row.get('native_cpu_reference_cases')==reference*count
            and bool(row.get('cpu_reference')) and bool(row.get('gpu_raw_output_sha256')))
