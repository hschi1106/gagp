"""Quality summaries over the frozen grid, with explicit unknowns and gen0."""
import study_statistics


def solved(score):
    count=score['cases']
    return count>0 and score['correct_count']==score['exact']==count and score['wrong']==0 and score['runtime_errors']==0


def outcome(search,common_jobs,documents,native_jobs):
    """Consume hash-checked, prevalidated common CPU judge documents.

    A runnable process alone is not a quality observation. Both splits require
    all selected generation/final native audits, with actual CPU counts separate.
    """
    if search.get('status')!='ok':return dict(complete=False,reason='search_'+search.get('status','missing'))
    generations=search['generations']
    for split in ('train','test'):
        common=common_jobs.get(split,{});native=native_jobs.get(split,{});doc=documents.get(split)
        if common.get('status')!='ok' or doc is None:return dict(complete=False,reason=split+'_common_missing_or_failed')
        count=common['cases']
        if len(doc['history'])!=generations or doc['cases']!=count:return dict(complete=False,reason=split+'_common_coverage')
        if (native.get('status')!='ok' or native.get('native_fidelity_pass') is not True
                or native.get('native_program_observations')!=generations+1
                or native.get('native_case_observations')!=(generations+1)*count
                or not 0<native.get('native_cpu_executed_programs',0)<=generations+1
                or native.get('native_cpu_executed_cases')!=native.get('native_cpu_executed_programs')*count
                or not native.get('native_mismatches') or any(native['native_mismatches'].values())):
            return dict(complete=False,reason=split+'_native_missing_failed_or_incomplete')
    split_results={}
    for split in ('train','test'):
        doc=documents[split];history=[solved(score) for score in doc['history']]
        final=solved(doc['final']);gen0=history[0]
        split_results[split]=dict(gen0=gen0,final=final,new_final=final and not gen0,
                                  lost_initial=gen0 and not final,history=history)
    a,b=split_results['train'],split_results['test']
    gen0=a['gen0'] and b['gen0'];final=a['final'] and b['final']
    split_results['joint']=dict(gen0=gen0,final=final,new_final=final and not gen0,
        lost_initial=gen0 and not final,history=[x and y for x,y in zip(a['history'],b['history'])])
    return dict(complete=True,reason='audited',splits=split_results)


def pair(task,seed,baseline,candidate,execution_fidelity=True):
    known=baseline.get('complete') and candidate.get('complete') and execution_fidelity
    return dict(task=task,seed=seed,complete_pair=bool(known),
        baseline_success=baseline['splits']['joint']['final'] if baseline.get('complete') else None,
        candidate_success=candidate['splits']['joint']['final'] if candidate.get('complete') else None,
        execution_fidelity=execution_fidelity,baseline=baseline,candidate=candidate)


def task_summary(rows,task,seeds,margin=.05):
    expected=set(seeds);lookup={}
    for row in rows:
        if row['task']!=task:continue
        seed=row['seed']
        if seed not in expected or seed in lookup:raise ValueError('unexpected or duplicate quality pair')
        lookup[seed]=row
    known=[lookup[s] for s in seeds if s in lookup and lookup[s]['complete_pair']]
    unknown=len(seeds)-len(known)
    base=[bool(row['baseline_success']) for row in known];cand=[bool(row['candidate_success']) for row in known]
    gained=sum(not a and b for a,b in zip(base,cand));lost=sum(a and not b for a,b in zip(base,cand))
    # Unknown outcomes can be either loss or gain. Enclose both extremes;
    # never let dropping missing rows tighten a noninferiority claim.
    low=study_statistics.paired_success(base+[True]*unknown,cand+[False]*unknown,margin)
    high=study_statistics.paired_success(base+[False]*unknown,cand+[True]*unknown,margin)
    return dict(task=task,expected_pairs=len(seeds),observed_pairs=len(known),unknown_pairs=unknown,
        baseline_success_observed=sum(base),candidate_success_observed=sum(cand),gained=gained,lost=lost,
        identification_range=[(gained-lost-unknown)/len(seeds),(gained-lost+unknown)/len(seeds)],
        difference_ci95=[low['difference_ci95'][0],high['difference_ci95'][1]],
        interval_method='conservative paired exact bounds enclosing all unknown assignments; per-task exploratory, not30 independent claims',
        supports_noninferiority=low['difference_ci95'][0]>-margin)
