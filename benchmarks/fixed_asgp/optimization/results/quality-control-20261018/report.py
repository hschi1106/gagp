"""Audit and report the frozen quality-control matrix, preserving every failure."""
from __future__ import annotations
from argparse import ArgumentParser
from collections import Counter,defaultdict
import csv
import fcntl
import io
import json
import math
from pathlib import Path
import statistics
import time

import analysis
import study
import validation_data
import run_validation

LABELS={'asgp':'ASGP 1T','gpu_base':'GPU baseline','gpu_exec':'GPU execution','gpu_operator':'GPU operator','gpu_guard':'GPU + CPU guard'}
COLORS={'asgp':'#475569','gpu_base':'#e09b36','gpu_exec':'#3182bd','gpu_operator':'#2a9d8f','gpu_guard':'#9b59b6'}


def csv_save(path,rows):
    if not rows:return
    fields=list(dict.fromkeys(key for row in rows for key in row))
    with path.open('w',newline='') as stream:
        writer=csv.DictWriter(stream,fieldnames=fields,lineterminator='\n');writer.writeheader();writer.writerows(rows)


def check_hash(path,digest,integrity):
    assert study.sha(path)==digest,'hash mismatch: '+str(path)
    integrity[str(path)]=digest


def validate_cpu_score(score,count):
    results=score['case_results']
    assert score['cases']==len(results)==count
    assert [r['case'] for r in results]==list(range(count)),'missing/duplicated judge case'
    assert all(r['exact'] in (0,1) and r['runtime_error'] in (0,1) for r in results)
    exact=sum(r['exact'] for r in results)
    assert score['correct_count']==score['exact']==exact
    assert score['wrong']==count-exact
    assert score['runtime_errors']==sum(r['runtime_error'] for r in results)
    assert math.isclose(score['exact_accuracy'],exact/count,rel_tol=0,abs_tol=1e-12)


def collect(allow_partial=False):
    freeze_path=study.BASE/'validation-freeze.json';freeze=study.read(freeze_path)
    matrix=study.RAW/'validation';manifest=study.read(matrix/'manifest.json');integrity={}
    check_hash(freeze_path,manifest['freeze_sha256'],integrity)
    check_hash(study.BASE/'candidate-selection.json',freeze['candidate_selection_sha256'],integrity)
    environment=study.read(study.BASE/'prevalidation-environment.json')
    check_hash(environment['allocator']['path'],environment['allocator']['sha256'],integrity)
    integrity[str(study.BASE/'prevalidation-environment.json')]=study.sha(study.BASE/'prevalidation-environment.json')
    check_hash(matrix/'bench',freeze['binary_sha256'],integrity)
    assert manifest['binary_sha256']==freeze['binary_sha256'] and manifest['settings']==freeze['settings']
    assert freeze['deadline_epoch']==study.DEADLINE and freeze['started_epoch']==study.START
    assert freeze['settings']['GAGP_HOST_THREADS']=='1'
    assert freeze['settings']['GAGP_GPU_PAYLOAD_WORKSPACE_MIB']=='16384'
    for path,digest in freeze['harness_sha256'].items():check_hash(path,digest,integrity)
    for path,digest in freeze['runtime_source_sha256'].items():check_hash(study.ROOT/path,digest,integrity)
    for path,digest in freeze['asgp_source']['files'].items():check_hash(Path(freeze['asgp_source']['root'])/path,digest,integrity)
    old=study.read(study.ROOT/'logs/day24-20261017/manifest.json')
    splits=[]
    for task in freeze['tasks']:
        entry=manifest.get('inputs',{}).get(task,dict(status='missing_input_preparation'))
        if entry['status']!='prepared':
            splits.append(dict(task=task,status=entry['status'],error=entry.get('error')));continue
        for field in ('pool','split','test_source'):check_hash(entry[field],entry[field+'_sha256'],integrity)
        check_hash(entry['grammar'],entry['grammar_sha256'],integrity)
        split=study.read(entry['split']);train=split['train'];test=split['test']
        train_ids={validation_data.identity(r) for r in train};test_ids={validation_data.identity(r) for r in test}
        previous=old['inputs'][task]
        previous_rows=sum([study.read(previous['quality']['0']['source'])['cases'],
            study.read(previous['test_source'])['cases'],study.read(previous['source'])['cases']],[])
        assert not train_ids & test_ids
        assert not test_ids & {validation_data.identity(r) for r in previous_rows}
        assert validation_data.digest_rows(train)==entry['metadata']['train_cases_sha256']
        assert validation_data.digest_rows(test)==entry['metadata']['test_cases_sha256']
        assert study.read(entry['test_source'])['cases']==test
        for seed in freeze['seeds']:
            row=entry['seeds'].get(str(seed),{})
            for field in ('source','frozen_source','prepared'):
                if field+'_sha256' in row:check_hash(row[field],row[field+'_sha256'],integrity)
            if 'source' in row:
                source=study.read(row['source']);initial=study.read(row['frozen_source'])
                assert source['population']==initial['population'] and len(source['population'])==1024
                assert source['population_seed']==seed and source['cases']==train
            if row.get('status')=='ok':assert len(study.read(row['prepared'])['programs'])==1024
        assert entry['metadata']['task']==task,'split metadata task differs'
        splits.append(dict(entry['metadata'],status='prepared'))
    rows=[];curves=[];generations=[];verification=[];judge_cases_sha={}
    for task in freeze['tasks']:
        entry=manifest.get('inputs',{}).get(task,dict(status='missing_input_preparation'))
        for seed in freeze['seeds']:
            for variant,spec in freeze['variants'].items():
                tag=f'{task}.s{seed}.{variant}';job=manifest['jobs'].get(tag,dict(status='missing'))
                judges={split:manifest['jobs'].get(tag+'.judge.'+split,dict(status='missing')) for split in ('train','test')}
                complete=job['status']=='ok' and all(j['status']=='ok' for j in judges.values())
                if not allow_partial:assert job['status']!='missing' and all(j['status']!='missing' for j in judges.values()),'coverage missing: '+tag
                row=dict(task=task,seed=seed,variant=variant,population=1024,generations=freeze['generations'],
                    evaluated_populations=freeze['generations']+1,search_status=job['status'],
                    train_audit_status=judges['train']['status'],test_audit_status=judges['test']['status'],audited=complete,
                    search_s=job.get('search_s'),process_s=job.get('process_s'),
                    train_audit_process_s=judges['train'].get('process_s'),test_audit_process_s=judges['test'].get('process_s'),
                    sampled_card_peak_gib=job.get('sampled_card_peak_bytes',0)/1024**3,
                    cpu_top16_fitness_mismatches=job.get('cpu_audit_fitness_mismatches'),
                    cpu_top16_wrong_mismatches=job.get('cpu_audit_wrong_mismatches'),
                    train_count=entry.get('metadata',{}).get('train_count'),test_count=entry.get('metadata',{}).get('test_count'),
                    final_train_accuracy=None,final_test_accuracy=None,final_train_wrong=None,final_test_wrong=None,
                    final_test_loss=None,final_test_runtime_errors=None,success=None,initial_success=None,new_final_success=None,
                    first_observed_joint_success_generation=None,first_observed_joint_success_s=None,
                    lost_initial_success=None,verification_programs=0,verification_cases=0,verification_ms=0,
                    verification_fitness_corrections=0,verification_wrong_corrections=0,verification_max_abs_error=0,
                    verification_best_unverified=0,verification_fraction=0,output=job.get('output'))
                if job['status']=='ok':
                    for field in ('output','export','history'):check_hash(job[field],job[field+'_sha256'],integrity)
                    doc=study.read(job['output']);run_validation.validate_run(doc,spec['mode'],1024,row['train_count'],freeze['generations'])
                    assert abs(job['search_s']-doc['evolve_wall_ms']/1000)<1e-9
                    assert job['extra']['GAGP_BM_SEED']==str(seed) and job['extra']['GAGP_SEARCH_GENERATIONS']==str(freeze['generations'])
                    expected_extra=dict(spec['extra'],GAGP_BM_CASE_COUNT=str(row['train_count']),GAGP_BM_SEED=str(seed),
                        GAGP_SEARCH_GENERATIONS=str(freeze['generations']),GAGP_SEARCH_EXPORT=job['export'],
                        GAGP_SEARCH_HISTORY_EXPORT=job['history'],GAGP_SEARCH_EXPORT_BEST_ONLY='1')
                    assert job['extra']==expected_extra,'formal extra settings differ'
                    inp=entry['seeds'][str(seed)];prepared=inp['prepared'] if inp['status']=='ok' else inp['source']
                    assert job['command']==[str(matrix/'bench'),'measure',inp['source'],prepared,entry['grammar'],'1024','search_'+spec['mode'],job['output']]
                    assert job['returncode']==0
                    for path,digest in job['input_sha256'].items():check_hash(path,digest,integrity)
                    row.update(final_unique_genomes=doc['unique_final_genomes'],final_mean_nodes=statistics.mean(p['nodes'] for p in doc['final_population']),
                        final_max_nodes=max(p['nodes'] for p in doc['final_population']))
                    v=doc.get('verification',{})
                    for field in ('programs','cases','fitness_corrections','wrong_corrections','max_abs_error','best_unverified','best_changes','errors','timeouts'):
                        row['verification_'+field]=v.get(field,0)
                    row['verification_ms']=v.get('wall_ms',0)
                    row['verification_fraction']=row['verification_ms']/doc['evolve_wall_ms']
                    for gen in doc['generations']:
                        common=dict(task=task,seed=seed,variant=variant,generation=gen['generation'])
                        generations.append(dict(common,**{k:gen.get(k) for k in ('best','best_wrong','best_nodes','mean','eval_ms','eval_kernel_ms','repro_ms','repro_kernel_ms','elapsed_search_ms','changed','unchanged','contract_rejections','budget_rejections','generation_rejections','crossover_attempts','mutation_attempts','payload_workspace_pool_allocations','payload_replay_cases','unscored')}))
                        verification.append(dict(common,**gen.get('verification',{})))
                if complete:
                    audited={}
                    for split,j in judges.items():
                        check_hash(j['output'],j['output_sha256'],integrity)
                        for path,digest in j['input_sha256'].items():check_hash(path,digest,integrity)
                        q=study.read(j['output']);assert len(q['history'])==freeze['generations']
                        inp=entry['seeds'][str(seed)]
                        judge_source=inp['source'] if split=='train' else entry['test_source']
                        assert j['command']==[str(matrix/'bench'),'quality-source',judge_source,job['export'],entry['grammar'],j['output'],job['history']]
                        assert j['extra']=={'GAGP_BM_CASE_COUNT':str(row[split+'_count'])} and j['returncode']==0
                        for score in q['history']+[q['final']]:validate_cpu_score(score,row[split+'_count'])
                        assert q['cases']==row[split+'_count']
                        assert q['task']==task and len(q['cases_sha256'])==64
                        key=(task,split)
                        if key in judge_cases_sha:assert q['cases_sha256']==judge_cases_sha[key],'judge cases differ across variants/seeds'
                        else:judge_cases_sha[key]=q['cases_sha256']
                        expected_backend='asgp_1t' if spec['mode']=='asgp_1t' else 'gagp_cpu_1t'
                        assert q['final']['backend']==expected_backend
                        assert all(g['backend']==expected_backend for g in q['history'])
                        assert len(q['final']['case_results'])==row[split+'_count']
                        assert all(len(g['case_results'])==row[split+'_count'] for g in q['history'])
                        audited[split]=q
                    tr,te=audited['train'],audited['test'];tf,vf=tr['final'],te['final']
                    success=tf['wrong']==vf['wrong']==0;initial=tr['history'][0]['wrong']==te['history'][0]['wrong']==0
                    row.update(final_train_accuracy=tf['exact_accuracy'],final_test_accuracy=vf['exact_accuracy'],
                        final_train_wrong=tf['wrong'],final_test_wrong=vf['wrong'],final_test_runtime_errors=vf['runtime_errors'],
                        final_test_loss=vf.get('capped_l1_mean',vf.get('exact_loss_mean')),success=success,initial_success=initial,
                        new_final_success=success and not initial,lost_initial_success=initial and not success)
                    times=[g['elapsed_search_ms']/1000 for g in doc['generations']]+[row['search_s']]
                    for generation,(a,b,elapsed) in enumerate(zip(tr['history']+[tf],te['history']+[vf],times)):
                        assert a['cases']==row['train_count'] and b['cases']==row['test_count']
                        if a['wrong']==b['wrong']==0 and row['first_observed_joint_success_generation'] is None:
                            row.update(first_observed_joint_success_generation=generation,first_observed_joint_success_s=elapsed)
                        curves.append(dict(task=task,seed=seed,variant=variant,generation=generation,elapsed_search_s=elapsed,
                            train_accuracy=a['exact_accuracy'],test_accuracy=b['exact_accuracy'],train_wrong=a['wrong'],test_wrong=b['wrong']))
                rows.append(row)
    planned=len(freeze['tasks'])*len(freeze['seeds'])*len(freeze['variants'])
    assert len(rows)==planned
    coverage=dict(planned_searches=planned,search_status=dict(Counter(r['search_status'] for r in rows)),
        train_audit_status=dict(Counter(r['train_audit_status'] for r in rows)),test_audit_status=dict(Counter(r['test_audit_status'] for r in rows)),
        fully_audited=sum(r['audited'] for r in rows),all_complete=all(r['audited'] for r in rows),
        frozen_before_fresh_dataset=all(j.get('started_epoch',math.inf)>=freeze['frozen_epoch'] for j in manifest['jobs'].values()))
    assert coverage['frozen_before_fresh_dataset']
    for task in freeze['tasks']:
        if (task,'train') in judge_cases_sha and (task,'test') in judge_cases_sha:
            assert judge_cases_sha[task,'train']!=judge_cases_sha[task,'test']
    assert all(j.get('started_epoch',study.START)>=study.START for j in manifest['jobs'].values())
    assert all(j.get('started_epoch',study.START)+j.get('process_s',0)<=study.DEADLINE for j in manifest['jobs'].values())
    intervals=sorted((j['started_epoch'],j['started_epoch']+j['process_s'],tag) for tag,j in manifest['jobs'].items() if 'process_s' in j)
    for left,right in zip(intervals,intervals[1:]):assert left[1]<=right[0]+.01,'timed validation jobs overlapped'
    return freeze,manifest,rows,curves,generations,verification,splits,integrity,coverage


def summaries(freeze,rows):
    details=[];by={(r['task'],r['seed'],r['variant']):r for r in rows}
    for task in freeze['tasks']:
        for variant in freeze['variants']:
            group=[by[task,s,variant] for s in freeze['seeds']];valid=[r for r in group if r['audited']]
            successes=sum(bool(r['success']) for r in valid);lo,hi=analysis.wilson(successes,len(group))
            details.append(dict(task=task,variant=variant,planned=len(group),audited=len(valid),
                successes=successes,operational_success_rate=successes/len(group),wilson95_low=lo,wilson95_high=hi,
                initial_champion_successes=sum(bool(r['initial_success']) for r in valid),
                new_final_successes=sum(bool(r['new_final_success']) for r in valid),
                lost_initial_successes=sum(bool(r['lost_initial_success']) for r in valid),
                median_train_accuracy=analysis.median([r['final_train_accuracy'] for r in valid]),
                median_test_accuracy=analysis.median([r['final_test_accuracy'] for r in valid]),
                median_search_s=analysis.median([r['search_s'] for r in group if r['search_status']=='ok']),
                median_verification_fraction=analysis.median([r['verification_fraction'] for r in group if r['search_status']=='ok']),
                fitness_mismatches=sum(r['cpu_top16_fitness_mismatches'] or 0 for r in group),
                wrong_mismatches=sum(r['cpu_top16_wrong_mismatches'] or 0 for r in group)))
    operator='gpu_operator' if 'gpu_operator' in freeze['variants'] else 'gpu_exec'
    comparisons=[('gpu_exec','gpu_base','execution'),(operator,'gpu_exec','operator'),('gpu_guard',operator,'guard')]
    comparisons=[x for x in comparisons if x[0]!=x[1]]
    comparisons +=[(variant,'asgp','vs_asgp') for variant in freeze['variants'] if variant!='asgp']
    pairs=[];tests=[]
    for a,b,family in comparisons:
        family_tests=[]
        for task in freeze['tasks']:
            valid=[];speeds=[]
            for seed in freeze['seeds']:
                x,y=by[task,seed,a],by[task,seed,b]
                ok=x['search_status']==y['search_status']=='ok';audited=x['audited'] and y['audited']
                ratio=y['search_s']/x['search_s'] if ok else None
                if ok:speeds.append(ratio)
                if audited:valid.append((x,y))
                pairs.append(dict(task=task,seed=seed,a=a,b=b,family=family,a_status=x['search_status'],b_status=y['search_status'],
                    audited=audited,a_search_s=x['search_s'],b_search_s=y['search_s'],speedup_b_over_a=ratio,
                    a_success=x['success'],b_success=y['success'],
                    accuracy_delta_a_minus_b=x['final_test_accuracy']-y['final_test_accuracy'] if audited else None))
            a_only=sum(x['success'] and not y['success'] for x,y in valid);b_only=sum(y['success'] and not x['success'] for x,y in valid)
            deltas=[x['final_test_accuracy']-y['final_test_accuracy'] for x,y in valid]
            lo,hi=analysis.bootstrap(deltas);slo,shi=analysis.bootstrap(speeds,statistics.median)
            family_tests.append(dict(task=task,a=a,b=b,family=family,paired_audited=len(valid),a_only=a_only,b_only=b_only,
                exact_mcnemar_p=analysis.mcnemar(a_only,b_only),mean_accuracy_delta=statistics.mean(deltas) if deltas else None,
                accuracy_delta_bootstrap95_low=lo,accuracy_delta_bootstrap95_high=hi,
                speed_pairs=len(speeds),median_speedup=analysis.median(speeds),geomean_speedup=analysis.geomean(speeds),
                speed_median_bootstrap95_low=slo,speed_median_bootstrap95_high=shi))
        adjusted=analysis.holm([r['exact_mcnemar_p'] for r in family_tests])
        for r,p in zip(family_tests,adjusted):r['holm_p_across9tasks']=p
        tests.extend(family_tests)
    return details,pairs,tests


def execution_identity(freeze,manifest,a_variant='gpu_base',b_variant='gpu_exec'):
    rows=[]
    for task in freeze['tasks']:
        for seed in freeze['seeds']:
            jobs=[manifest['jobs'].get(f'{task}.s{seed}.{variant}',dict(status='missing')) for variant in (a_variant,b_variant)]
            row=dict(task=task,seed=seed,a=a_variant,b=b_variant,status='ok' if all(j['status']=='ok' for j in jobs) else 'unavailable')
            if row['status']=='ok':
                a,b=[study.read(j['output']) for j in jobs]
                reference=b.get('verification',{})
                row.update(reference_fitness_corrections=reference.get('fitness_corrections',0),reference_wrong_corrections=reference.get('wrong_corrections',0))
                ah,bh=[study.read(j['history'])['history'] for j in jobs]
                row.update(final_champion_ast_equal=study.read(jobs[0]['export'])==study.read(jobs[1]['export']),
                    all_observed_champion_asts_equal=[g['ast'] for g in ah]==[g['ast'] for g in bh],
                    all_observed_champion_fitness_equal=[g['fitness'] for g in ah]==[g['fitness'] for g in bh],
                    final_score_wrong_size_arrays_equal=a['final_population']==b['final_population'])
            rows.append(row)
    return rows


def diagnostic_tables():
    initial=study.read(study.RAW/'initial/summary.json')['rows']
    sites=study.read(study.RAW/'site-admission/summary.json')['rows']
    checkpoints=study.read(study.RAW/'checkpoint-metrics/summary.json')['rows']
    training=[];variation=[];guard=[]
    for phase in ('development','site-development','guard-development-v2'):
        m=study.read(study.RAW/phase/'manifest.json')
        for tag,job in m['jobs'].items():
            if 'task'not in job or 'split'in job:continue
            if job['status']!='ok':continue
            d=study.read(job['output'])
            for g in d['generations']:
                variation.append(dict(phase=phase,task=job['task'],seed=job['seed'],variant=job['variant'],
                    generation=g['generation'],**{k:g.get(k) for k in ('best_wrong','best_nodes','mean_nodes','changed','unchanged','crossover_attempts','mutation_attempts','contract_rejections','budget_rejections','generation_rejections','eval_ms','repro_ms')}))
            if phase=='guard-development-v2':
                guard.append(dict(task=job['task'],variant=job['variant'],search_s=d['evolve_wall_ms']/1000,**d['verification']))
    for phase in ('development-training-judges','site-training-judges'):
        for row in study.read(study.RAW/phase/'summary.json')['rows']:training.append(dict(phase=phase,**row))
    evolved=[]
    for task in study.TASKS:
        for generation in (8,32,64):
            folder=study.RAW/'checkpoint-audit';tag=f'{task}.asgp.g{generation}'
            source=folder/(tag+'.asgp_snapshot.json');native=folder/(tag+'.snapshot.json')
            row=dict(task=task,generation=generation,requested_population=1024)
            if not source.exists() or not native.exists():row.update(status='missing_or_failed_snapshot');evolved.append(row);continue
            a=study.read(source)['programs'];b=study.read(native)['programs'];assert len(a)==len(b)
            admission=study.read(folder/(tag+'.admission.json'))
            row.update(status='ok',admitted=len(a),rejected=1024-len(a),
                rejection_reasons=json.dumps(dict(Counter(r['reason'] for r in admission['rejections'])),sort_keys=True),
                source_native_cpu_fitness_mismatches=sum(study.canonical(x['fitness'])!=study.canonical(y['cpu_fitness']) for x,y in zip(a,b)),
                source_native_cpu_wrong_mismatches=sum(x['wrong']!=y['cpu_wrong'] for x,y in zip(a,b)),
                raw_max_abs_error=max([abs(x['fitness']-y['cpu_fitness']) for x,y in zip(a,b)] or [0]))
            evolved.append(row)
    profiles=[]
    for phase in ('profile-baseline','profile-tuple'):
        for task in ('is_sorted','all_equal'):
            def table(kind):
                suffix='.'+kind+'.csv' if phase=='profile-baseline' and task=='is_sorted' else '.stats_'+kind+'.csv'
                lines=(study.RAW/phase/(task+suffix)).read_text().splitlines()
                start=next(i for i,line in enumerate(lines) if line.startswith('Time (%)'))
                return list(csv.DictReader(io.StringIO('\n'.join(lines[start:]))))
            kernels=table('cuda_gpu_kern_sum');apis=table('cuda_api_sum')
            profiles.append(dict(phase=phase,task=task,kernel_total_s=sum(int(r['Total Time (ns)']) for r in kernels)/1e9,
                fitness_kernel_s=sum(int(r['Total Time (ns)']) for r in kernels if 'evaluate_fitness_programs_impl' in r['Name'])/1e9,
                malloc_calls=sum(int(r['Num Calls']) for r in apis if r['Name']=='cudaMalloc'),
                malloc_api_interval_s=sum(int(r['Total Time (ns)']) for r in apis if r['Name']=='cudaMalloc')/1e9))
    return dict(initial=initial,sites=sites,checkpoints=checkpoints,development_training=training,development_variation=variation,guard_development=guard,profiles=profiles,evolved_source_comparison=evolved)


def plots(out,freeze,details,tests,diagnostics):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    import numpy as np
    plt.rcParams.update({'font.size':9,'axes.spines.top':False,'axes.spines.right':False,'savefig.bbox':'tight'})
    variants=list(freeze['variants']);tasks=freeze['tasks'];x=np.arange(len(tasks));width=.8/len(variants)
    fig,(ax,bx)=plt.subplots(2,1,figsize=(11,8),layout='constrained')
    for i,v in enumerate(variants):
        group=[next(r for r in details if r['task']==t and r['variant']==v) for t in tasks]
        values=np.array([r['operational_success_rate'] for r in group]);low=np.array([r['wilson95_low'] for r in group]);high=np.array([r['wilson95_high'] for r in group])
        ax.bar(x+(i-(len(variants)-1)/2)*width,values,width,label=LABELS[v],color=COLORS[v],yerr=[values-low,high-values],error_kw=dict(linewidth=.6,capsize=1))
    ax.set(ylim=(0,1.12),ylabel='Final joint train/test success / planned seeds',xticks=x,xticklabels=[t.replace('_',' ') for t in tasks]);ax.tick_params(axis='x',rotation=22)
    ax.legend(ncol=3,frameon=False);ax.set_title(f'P1024, G{freeze["generations"]}, {len(freeze["seeds"])} fresh paired seeds; bars: Wilson 95%')
    for i,v in enumerate(variants[1:]):
        group=[next(r for r in tests if r['task']==t and r['a']==v and r['b']=='asgp') for t in tasks]
        values=np.array([r['median_speedup'] or math.nan for r in group]);low=np.array([r['speed_median_bootstrap95_low'] or math.nan for r in group]);high=np.array([r['speed_median_bootstrap95_high'] or math.nan for r in group])
        bx.errorbar(x+(i-(len(variants)-2)/2)*.12,values,yerr=[values-low,high-values],fmt='o',markersize=4,label=LABELS[v],color=COLORS[v],capsize=2)
    bx.axhline(1,color='#777',linestyle='--',linewidth=.8);bx.set(yscale='log',ylabel='ASGP search wall / GPU search wall',xticks=x,xticklabels=[t.replace('_',' ') for t in tasks]);bx.tick_params(axis='x',rotation=22)
    bx.legend(ncol=2,frameon=False);bx.set_title('Fixed-workload speed; per-task paired median with bootstrap 95%')
    for ext in ('png','pdf'):fig.savefig(out/('speed-quality.'+ext),dpi=170)
    plt.close(fig)
    fig,axes=plt.subplots(3,3,figsize=(11,8),layout='constrained')
    for ax,t in zip(axes.flat,tasks):
        for v in ('asgp','baseline','site7','site7_parsimony'):
            group=sorted([r for r in diagnostics['checkpoints'] if r['task']==t and r['variant']==v and r['status']=='ok'],key=lambda r:r['generation'])
            ax.plot([r['generation'] for r in group],[r['unique_cpu_behavior_probes']/r['population'] for r in group],marker='o',markersize=3,label=v)
        ax.set(title=t.replace('_',' '),ylim=(0,1),xticks=[0,8,32,64],xlabel='Generation',ylabel='Distinct CPU probes / admitted P')
    axes[0,0].legend(fontsize=7,frameon=False);fig.suptitle('Behavior diversity on first32 OLD training cases; source admission exclusions reported')
    for ext in ('png','pdf'):fig.savefig(out/('diagnostic-diversity.'+ext),dpi=170)
    plt.close(fig)
    fig,(ax,bx)=plt.subplots(1,2,figsize=(10,4),layout='constrained')
    for i,v in enumerate(('exec','guard1_i8','guard8_i8','guard32_i1')):
        group=[next(r for r in diagnostics['guard_development'] if r['task']==t and r['variant']==v) for t in tasks]
        ax.plot(x,[r['wall_ms']/1000 for r in group],marker='o',markersize=3,label=v)
        base={r['task']:r['search_s'] for r in diagnostics['guard_development'] if r['variant']=='exec'}
        bx.plot(x,[r['search_s']/base[r['task']] for r in group],marker='o',markersize=3,label=v)
    for a in (ax,bx):a.set_xticks(x,[t.replace('_',' ') for t in tasks],rotation=50,ha='right');a.legend(frameon=False,fontsize=7)
    ax.set(ylabel='CPU verification wall (s)',title='Explicit cost, old training seed0/G64');bx.set(ylabel='Search wall / execution-only wall',title='Cost screen; one seed, no quality inference')
    for ext in ('png','pdf'):fig.savefig(out/('verification-cost.'+ext),dpi=170)
    plt.close(fig)


def main():
    parser=ArgumentParser(description=__doc__);parser.add_argument('--out',type=Path,required=True);parser.add_argument('--allow-partial',action='store_true');args=parser.parse_args()
    run_validation.preflight()
    assert not args.out.exists(),'report destination must be NEW'
    with open('/tmp/gagp-support-20261015-gpu.lock','a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        freeze,manifest,rows,curves,generations,verification,splits,integrity,coverage=collect(args.allow_partial)
        details,pairs,tests=summaries(freeze,rows);diagnostics=diagnostic_tables();diagnostics['execution_identity']=execution_identity(freeze,manifest)
        operator='gpu_operator' if 'gpu_operator' in freeze['variants'] else 'gpu_exec'
        diagnostics['verification_identity']=execution_identity(freeze,manifest,operator,'gpu_guard')
        args.out.mkdir(parents=True)
        for name,data in dict(runs=rows,curves=curves,generations=generations,verification=verification,task_summary=details,paired_runs=pairs,paired_tests=tests,dataset_splits=splits,**diagnostics).items():csv_save(args.out/(name+'.csv'),data)
        study.save(args.out/'coverage.json',coverage);study.save(args.out/'integrity.json',integrity)
        study.save(args.out/'summary.json',dict(coverage=coverage,freeze=freeze,details=details,paired_tests=tests))
        plots(args.out,freeze,details,tests,diagnostics)
        # Prose and interactive rendering are maintained separately from collection.
        import report_view
        report_view.write(args.out,freeze,coverage,rows,curves,details,tests,diagnostics)
        study.save(args.out/'completion.json',dict(finished_epoch=time.time(),original_deadline_epoch=study.DEADLINE,
            within_original_24h=time.time()<=study.DEADLINE,all_complete=coverage['all_complete'],
            report_source_sha256={str(study.BASE/p):study.sha(study.BASE/p) for p in ('report.py','analysis.py','report_view.py','dashboard.template.html')}))
    print(json.dumps(coverage,indent=2))


if __name__=='__main__':main()
