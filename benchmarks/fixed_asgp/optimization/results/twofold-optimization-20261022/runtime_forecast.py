"""Conservative cost accounting from development measurements, never heldout outcomes."""
import math


def positive(value):
    if not math.isfinite(value) or value<=0:raise ValueError('runtime must be finite and positive')
    return value


def complete(rows,tasks,extra):
    expected={(task,variant) for task in tasks for variant in extra};lookup={}
    for row in rows:
        key=(row['task'],row['variant'])
        if key in lookup or key not in expected:raise ValueError('duplicate or unexpected runtime observation')
        lookup[key]=row
    if set(lookup)!=expected:raise ValueError('incomplete runtime grid')
    return lookup


def project_search(data,process_s,generations):
    process_s=positive(process_s)
    rows=data['generations'];source_generations=len(rows)
    if not rows or generations<source_generations:raise ValueError('unsupported search scale forecast')
    times=[positive(row['generation_ms']/1000) for row in rows]
    wall=positive(data['evolve_wall_ms']/1000)
    if process_s+0.01<wall or math.fsum(times)>process_s+0.01:raise ValueError('inconsistent process/search measurements')
    # Initialization, final evaluation and process/CPU16 costs are paid once.
    # Use the slower of the full pilot and its latter half for added generations.
    latter=times[source_generations//2:]
    per_generation=max(math.fsum(times)/source_generations,math.fsum(latter)/len(latter))
    return process_s+(generations-source_generations)*per_generation


def full_audit_cost(historical_rows,tasks,populations,generations):
    """Old complete native CPU costs are FORECAST inputs only, never speed denominators.

    Use the slower old GPU method percell for each of the two new native audits.
    No optimistic discount for exact-AST dedup. Conservatively scale by G/8;
    each current successful formal search still needs its own new native audit.
    """
    expected={(task,population,variant) for task in tasks for population in populations
              for variant in ('gpu_original','gpu_improved')};lookup={}
    for row in historical_rows:
        if row['variant'] not in ('gpu_original','gpu_improved') or int(row['repetition'])!=0:continue
        key=(row['task'],int(row['population']),row['variant'])
        if key not in expected or key in lookup:raise ValueError('unexpected or duplicate historical full audit')
        if row['status']!='ok' or str(row['full_final_cpu_audit'])!='True' or int(row['generations'])!=8:
            raise ValueError('historical CPU forecast row is not a successful full audit')
        if int(row['cpu_audit_program_count'])!=int(row['population']):raise ValueError('top16 is not full CPU cost evidence')
        lookup[key]=positive(float(row['cpu_audit_s']))
    if set(lookup)!=expected:raise ValueError('incomplete historical full-CPU forecast grid')
    if generations<8:raise ValueError('unsupported native audit scale')
    return 2*math.fsum(max(lookup[task,population,variant] for variant in ('gpu_original','gpu_improved'))
                       for task in tasks for population in populations)*(generations/8)


def quality_seed_cost(training,timings,preparation,tasks):
    train=complete(training,tasks,('baseline','candidate'))
    timing=complete(timings,tasks,('baseline','candidate'))
    prep={row['task']:row for row in preparation}
    if len(prep)!=len(preparation) or set(prep)!=set(tasks):raise ValueError('incomplete or duplicate preparation cost grid')
    search=audits=prepare=0.
    for task in tasks:
        row=prep[task]
        if row.get('heldout') is not False or row['seed']!=0:raise ValueError('preparation forecast must use old seed0')
        prepare+=positive(row['freeze_s'])+positive(row['prepare_s'])
        for variant in ('baseline','candidate'):
            a,b=train[task,variant],timing[task,variant]
            if a.get('status')!='ok' or a['generations']!=64 or a['population']!=1024:
                raise ValueError('training runtime contract is incomplete')
            if b.get('heldout') is not False or a['cases']!=b['training_cases'] or a['cases']<=0:
                raise ValueError('training replica is not valid old TRAINING evidence')
            scale=max(1.,200/a['cases'])
            process=positive(a['process_s']);evaluation=positive(a['evaluation_s'])
            if evaluation>process:raise ValueError('evaluation exceeds process wall')
            search+=process+(scale-1)*evaluation
            audits+=positive(a['native_train_s'])*scale
            audits+=positive(b['common_training_s'])*scale
            audits+=positive(b['common_repeat1000_s'])+positive(b['native_repeat1000_s'])
    return dict(total_s=search+audits+prepare,search_process_s=search,
                common_and_native_audit_process_s=audits,preparation_process_s=prepare,
                scope='all30, both variants, G64/P1024, one independent seed; old TRAINING runtime only; finite training scaled conservatively toward200')
