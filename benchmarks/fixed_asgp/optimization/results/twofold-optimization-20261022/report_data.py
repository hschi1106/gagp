"""Read complete or partial frozen matrices without dropping failures."""
import math
from pathlib import Path

import champion_audit_dedup
import formal_dedup
import full_population_audit
import quality_dedup
import quality_metrics
import report_qualification
import reference_reader
import serial
import study_statistics


class Evidence:
    def __init__(self):
        self.hashes={}

    def checked(self,path,digest):
        path=Path(path)
        if str(path) in self.hashes:
            if self.hashes[str(path)]!=digest:raise ValueError('conflicting recorded artifact hashes')
        else:
            if serial.sha(path)!=digest:raise ValueError('artifact hash differs: '+str(path))
            self.hashes[str(path)]=digest
        return serial.read(path)

    def output(self,job,field='output'):
        return self.checked(job[field],job[field+'_sha256'])

    def stage(self,name,spec):
        path=serial.RAW/name/'manifest.json'
        if not path.exists():return dict(status='missing',jobs={})
        stage=serial.read(path)
        if stage['binary_sha256']!=spec['binary_sha256']:raise ValueError('stage binary differs from freeze')
        binary=path.parent/'bench'
        if serial.sha(binary)!=spec['binary_sha256']:raise ValueError('pinned stage binary changed')
        self.hashes[str(binary)]=spec['binary_sha256']
        return stage


def history_equal(a,b):
    return [(g.get('ast',g.get('program')),g['fitness']) for g in a['history']]==[(g.get('ast',g.get('program')),g['fitness']) for g in b['history']]


def read_speed(name,freeze,evidence):
    stages={v:evidence.stage(name+'-'+v,spec) for v,spec in freeze['variants'].items()}
    native={v:evidence.stage(name+'-full-native-'+v,spec) for v,spec in freeze['variants'].items()}
    comparisons=[];runs=[];audits=[];documents={}
    costs=dict(search_process_s=0.,inline_cpu_audit_s=0.,native_audit_process_s=0.,
               native_cpu_program_executions=0,native_cpu_case_executions=0,
               native_logical_program_observations=0,native_logical_case_observations=0,
               native_controller_cpu_s=0.,native_cpu_reference_program_uses=0,native_cpu_reference_case_uses=0)
    for task in freeze['tasks']:
        for population in freeze['populations']:
            for repetition in range(freeze['repetitions']):
                tag=f'{task}.p{population}.s0.g{freeze["generations"]}.r{repetition}'
                pair={};statuses=[]
                for variant in ('baseline','candidate'):
                    job=stages[variant]['jobs'].get(tag,dict(status='missing'))
                    costs['search_process_s']+=job.get('process_s',0.)
                    statuses.append(job['status'])
                    row=dict(task=task,population=population,repetition=repetition,variant=variant,
                             seed=0,generations=freeze['generations'],status=job['status'],process_s=job.get('process_s'),
                             sampled_card_peak_bytes=job.get('sampled_card_peak_bytes'))
                    if job['status']=='ok':
                        data=evidence.output(job);formal_dedup.validate(data,population,freeze['generations'],False)
                        export=evidence.output(job,'export');history=evidence.output(job,'history')
                        if len(export['programs'])!=(population if repetition==0 else 1):raise ValueError('wrong final AST export coverage')
                        pair[variant]=(data,export,history,job)
                        documents[task,population,variant,repetition]=data
                        costs['inline_cpu_audit_s']+=data['cpu_audit_ms']/1000
                        row.update(search_s=data['evolve_wall_ms']/1000,cpu_audit_s=data['cpu_audit_ms']/1000,
                                   cpu_audit_program_count=data['cpu_audit_program_count'],
                                   stage_reserved_bytes=data.get('gpu_stage_storage_reserved_bytes',0),
                                   stage_peak_bytes=data.get('gpu_stage_storage_peak_bytes',0),
                                   reproduction_profile=data['reproduction_profile'])
                    runs.append(row)
                comparison=dict(task=task,population=population,repetition=repetition,statuses=statuses)
                if len(pair)==2:
                    a,b=pair['baseline'],pair['candidate']
                    comparison.update(speedup=a[0]['evolve_wall_ms']/b[0]['evolve_wall_ms'],
                        final_descriptors_equal=a[0]['final_population']==b[0]['final_population'],
                        champion_ast_fitness_equal=history_equal(a[2],b[2]),
                        final_export_equal=a[3]['export_sha256']==b[3]['export_sha256'] and a[1]==b[1],
                        full_final_ast_comparison=repetition==0)
                comparisons.append(comparison)
            for variant in ('baseline','candidate'):
                tag=f'{task}.p{population}.{variant}.final.full-native'
                job=native[variant]['jobs'].get(tag,dict(status='missing',task=task,population=population,variant=variant))
                record=dict(job)
                costs['native_audit_process_s']+=job.get('process_s',0.)
                costs['native_controller_cpu_s']+=job.get('native_controller_cpu_s',0.)
                if job['status']=='ok':
                    search=documents[task,population,variant,0]
                    expanded=evidence.output(job)
                    reference=native['baseline']['jobs'].get(f'{task}.p{population}.baseline.final.full-native') if job.get('cpu_reference') else None
                    reproduced=reference_reader.reconstruct(job,evidence,freeze['settings'],reference)
                    if reproduced!=expanded:raise ValueError('native expansion does not match raw unique executions')
                    checks=full_population_audit.validate(expanded,search,population,1024)
                    if any(job.get(k)!=v for k,v in checks.items()):raise ValueError('native audit record differs from recomputed evidence')
                    record.update(checks)
                    costs['native_cpu_reference_program_uses']+=job.get('native_cpu_reference_programs',0)
                    costs['native_cpu_reference_case_uses']+=job.get('native_cpu_reference_cases',0)
                    for dest,key in [('native_cpu_program_executions','native_cpu_executed_programs'),
                                     ('native_cpu_case_executions','native_cpu_executed_cases'),
                                     ('native_logical_program_observations','native_program_observations'),
                                     ('native_logical_case_observations','native_case_observations')]:costs[dest]+=job[key]
                audits.append(record)
    summary=report_qualification.speed_with_native_audits(comparisons,audits,freeze['tasks'],freeze['populations'],freeze['repetitions'])
    for cell in summary['cells']:
        for variant in ('baseline','candidate'):
            values=[documents[(cell['task'],cell['population'],variant,rep)]['evolve_wall_ms']/1000
                    for rep in range(freeze['repetitions'])] if cell['timing_qualified'] else []
            cell['observed_'+variant+'_search_s']=study_statistics.geometric_mean(values) if values else None
    # Sensitivity across the frozen cold repetitions; no task-resampling inference.
    for label,group in summary['groups'].items():
        allowed=freeze['populations'] if label=='pooled' else [int(label)]
        qualified=[c for c in summary['cells'] if c['population'] in allowed and c['qualified']]
        for variant in ('baseline','candidate'):
            values=[c['observed_'+variant+'_search_s'] for c in qualified]
            group['observed_'+variant+'_search_s']=study_statistics.geometric_mean(values) if values else None
        if qualified:
            assert math.isclose(group['observed_baseline_search_s']/group['observed_candidate_search_s'],
                                group['observed_geometric_mean'],rel_tol=1e-10)
        by_repeat=[]
        for rep in range(freeze['repetitions']):
            rows=[r for r in comparisons if r['population'] in allowed and r['repetition']==rep]
            if group['native_audit_pass'] and len(rows)==len(freeze['tasks'])*len(allowed) and all(r.get('statuses')==['ok','ok'] and
                    all(r.get(k) for k in ('final_descriptors_equal','champion_ast_fitness_equal','final_export_equal')) for r in rows):
                by_repeat.append(study_statistics.geometric_mean(r['speedup'] for r in rows))
        group['cold_repeat_geometric_means']=by_repeat
        group['cold_repeat_range']=[min(by_repeat),max(by_repeat)] if by_repeat else None
    return dict(summary=summary,runs=runs,comparisons=comparisons,native_audits=audits,costs=costs)


def compact_judge(document):
    return dict(document,history=[{k:v for k,v in row.items() if k!='case_results'} for row in document['history']],
                final={k:v for k,v in document['final'].items() if k!='case_results'})


def read_quality(name,freeze,evidence):
    stages={v:evidence.stage(name+'-'+v,spec) for v,spec in freeze['variants'].items()}
    judges={v:evidence.stage(name+'-'+v+'-judges',spec) for v,spec in freeze['variants'].items()}
    preparation=evidence.stage(name+'-inputs',freeze['variants']['candidate'])
    pairs=[];runs=[];costs=dict(preparation_process_s=math.fsum(j.get('process_s',0.) for j in preparation['jobs'].values()),
                              search_process_s=0.,common_judge_process_s=0.,native_judge_process_s=0.,
                              common_cpu_program_executions=0,common_cpu_case_executions=0,native_cpu_program_executions=0,native_cpu_case_executions=0,
                              native_logical_program_observations=0,native_logical_case_observations=0)
    for seed in freeze['seeds']:
        for task in freeze['tasks']:
            outcomes={};searches={}
            for variant in ('baseline','candidate'):
                tag=f'{task}.s{seed}.{variant}';job=stages[variant]['jobs'].get(tag,dict(status='missing',generations=freeze['generations']))
                searches[variant]=job
                row=dict(task=task,seed=seed,variant=variant,population=1024,generations=freeze['generations'],
                         status=job['status'],process_s=job.get('process_s'))
                documents={};common_jobs={};native_jobs={}
                costs['search_process_s']+=job.get('process_s',0.)
                if job['status']=='ok':
                    search=evidence.output(job);quality_dedup.validate(search,search['cases'],freeze['generations'])
                    evidence.output(job,'export');evidence.output(job,'history')
                    row.update(search_s=search['evolve_wall_ms']/1000,cpu_audit_s=search['cpu_audit_ms']/1000,
                               sampled_card_peak_bytes=job.get('sampled_card_peak_bytes'))
                for split in ('train','test'):
                    common=judges[variant]['jobs'].get(tag+'.'+split,dict(status='missing'))
                    native=judges[variant]['jobs'].get(tag+'.'+split+'.native',dict(status='missing'))
                    common_jobs[split]=common;native_jobs[split]=native
                    row[split+'_common_status']=common['status'];row[split+'_native_status']=native['status']
                    costs['common_judge_process_s']+=common.get('process_s',0.)
                    costs['native_judge_process_s']+=native.get('process_s',0.)
                    if (common['status']=='ok' or native['status']=='ok') and job['status']!='ok':
                        raise ValueError('successful judge without a successful validated search')
                    if common['status']=='ok':
                        doc=evidence.output(common);quality_dedup.validate_judge(doc,common['cases'],freeze['generations'],task)
                        documents[split]=compact_judge(doc)
                        costs['common_cpu_program_executions']+=doc['unique_audited_programs']
                        costs['common_cpu_case_executions']+=doc['unique_audited_programs']*common['cases']
                    if native['status']=='ok':
                        doc=evidence.output(native);raw=evidence.output(native,'raw_output');mapping=evidence.output(native,'mapping')
                        if champion_audit_dedup.expand_document(raw,mapping['observation_to_unique'])!=doc:
                            raise ValueError('native champion expansion differs from actual executions')
                        checked=champion_audit_dedup.validate(doc,search,split,native['cases'])
                        if any(native.get(k)!=v for k,v in checked.items()):raise ValueError('native champion metadata mismatch')
                        for dest,key in [('native_cpu_program_executions','native_cpu_executed_programs'),
                                         ('native_cpu_case_executions','native_cpu_executed_cases'),
                                         ('native_logical_program_observations','native_program_observations'),
                                         ('native_logical_case_observations','native_case_observations')]:costs[dest]+=native[key]
                observed=quality_metrics.outcome(job,common_jobs,documents,native_jobs)
                outcomes[variant]=observed;row.update(quality_audited=observed['complete'],quality_reason=observed['reason'])
                if observed['complete']:
                    for split in ('train','test','joint'):
                        for key in ('gen0','final','new_final','lost_initial'):row[split+'_'+key]=observed['splits'][split][key]
                runs.append(row)
            fidelity=False
            if all(searches[v]['status']=='ok' for v in ('baseline','candidate')):
                a,b=[searches[v] for v in ('baseline','candidate')]
                x,y=[evidence.output(j) for j in (a,b)]
                fidelity=(x['final_population']==y['final_population'] and
                    history_equal(evidence.output(a,'history'),evidence.output(b,'history')) and a['export_sha256']==b['export_sha256'])
            pairs.append(quality_metrics.pair(task,seed,outcomes['baseline'],outcomes['candidate'],fidelity))
    tasks=[quality_metrics.task_summary(pairs,task,freeze['seeds']) for task in freeze['tasks']]
    bound=study_statistics.clustered_quality_bound(pairs,freeze['tasks'],freeze['seeds'])
    return dict(runs=runs,pairs=pairs,tasks=tasks,clustered_bound=bound,costs=costs)
