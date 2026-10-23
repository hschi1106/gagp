"""Closed-coverage speed summaries and conservative paired binary intervals."""
import math


def geometric_mean(values):
    values=list(values)
    if not values or any(not math.isfinite(v) or v<=0 for v in values):
        raise ValueError('geometric mean requires finite positive observations')
    return math.exp(math.fsum(math.log(v) for v in values)/len(values))


def binomial_cdf(k,n,p):
    if n<0 or not 0<=p<=1:raise ValueError('invalid binomial parameters')
    if k<0:return 0.0
    if k>=n:return 1.0
    if p==0:return 1.0
    if p==1:return 0.0
    logs=[math.lgamma(n+1)-math.lgamma(i+1)-math.lgamma(n-i+1)+
          i*math.log(p)+(n-i)*math.log1p(-p) for i in range(k+1)]
    peak=max(logs)
    return min(1.0,math.exp(peak)*math.fsum(math.exp(v-peak) for v in logs))


def clopper_pearson(k,n,alpha=.05):
    """Equal-tailed exact binomial confidence interval, numerical inversion."""
    if n<1 or not 0<=k<=n or not 0<alpha<1:raise ValueError('invalid count/coverage')
    def solve(cut,target):
        lo,hi=0.,1.
        for _ in range(60):
            mid=(lo+hi)/2
            if binomial_cdf(cut,n,mid)>target:lo=mid
            else:hi=mid
        return (lo+hi)/2
    return (0. if k==0 else solve(k-1,1-alpha/2),
            1. if k==n else solve(k,alpha/2))


def paired_success(baseline,candidate,margin=.05):
    """Paired Bernoulli difference; failures must already be included as false.

    Use Bonferroni exact intervals for the two discordant marginal probabilities,
    then subtract their limits. The resulting two-sided95% interval is
    conservative, including when every observed pair agrees. A degenerate
    bootstrap interval at zero must not be treated as proof of equivalence.
    """
    if len(baseline)!=len(candidate) or not baseline:raise ValueError('missing paired outcomes')
    n=len(baseline)
    gained=sum(not a and b for a,b in zip(baseline,candidate))
    lost=sum(a and not b for a,b in zip(baseline,candidate))
    gain_ci=clopper_pearson(gained,n,.025)
    loss_ci=clopper_pearson(lost,n,.025)
    interval=(gain_ci[0]-loss_ci[1],gain_ci[1]-loss_ci[0])
    return dict(pairs=n,baseline_success=sum(baseline),candidate_success=sum(candidate),
                gained=gained,lost=lost,difference=(gained-lost)/n,
                difference_ci95=interval,margin=margin,
                supports_noninferiority=interval[0]>-margin,
                interval_method='Bonferroni Clopper-Pearson bounds on discordant probabilities')


def speed_summary(comparisons,tasks,populations,repetitions):
    """Never turn a successful subset into full-matrix target attainment."""
    keys=[(t,p,r) for t in tasks for p in populations for r in range(repetitions)]
    rows={}
    for row in comparisons:
        key=(row['task'],row['population'],row['repetition'])
        if key in rows:raise ValueError('duplicate speed observation')
        if key not in keys:raise ValueError('unexpected speed observation')
        rows[key]=row
    cells=[]
    fidelity=('final_descriptors_equal','champion_ast_fitness_equal','final_export_equal')
    for task in tasks:
        for population in populations:
            ratios=[];failures=[]
            for rep in range(repetitions):
                row=rows.get((task,population,rep))
                if row is None:failures.append('missing');continue
                if row.get('statuses')!=['ok','ok']:
                    failures.append('/'.join(row.get('statuses',['unknown'])));continue
                if not all(row.get(k) is True for k in fidelity):
                    failures.append('execution_fidelity_mismatch');continue
                ratio=row.get('speedup',0)
                if not math.isfinite(ratio) or ratio<=0:failures.append('invalid_ratio');continue
                ratios.append(ratio)
            cells.append(dict(task=task,population=population,repetitions=len(ratios),failures=failures,
                speedup=geometric_mean(ratios) if ratios else None,
                qualified=len(ratios)==repetitions and not failures))
    groups={}
    for label,pops in [(str(p),[p]) for p in populations]+[('pooled',populations)]:
        observed=[c for c in cells if c['population'] in pops and c['qualified']]
        expected=len(tasks)*len(pops)
        value=geometric_mean(c['speedup'] for c in observed) if observed else None
        groups[label]=dict(expected_cells=expected,qualified_cells=len(observed),
            observed_geometric_mean=value,full_coverage=len(observed)==expected,
            target_reached=len(observed)==expected and value>=2.)
    return dict(cells=cells,groups=groups,all_primary_targets_reached=all(g['target_reached'] for g in groups.values()))


def clustered_quality_bound(rows,tasks,seeds,margin=.05):
    """Simultaneous loss bound for the fixed task set, with seed as the unit.

    Tasks sharing one evolution seed are not independent trials. A seed is bad
    if ANY task loses a baseline success, or an expected comparison is unknown.
    Each task's loss probability is bounded by this union event. The upper
    Clopper-Pearson limit (one-sided97.5%) therefore gives a conservative lower
    bound on every fixed task's paired success difference. Gains are ignored.
    Unknown comparisons are counted as losses, not silently removed.
    """
    if not tasks or not seeds or len(set(tasks))!=len(tasks) or len(set(seeds))!=len(seeds):
        raise ValueError('invalid expected quality grid')
    expected={(task,seed) for task in tasks for seed in seeds}
    lookup={}
    for row in rows:
        key=(row['task'],row['seed'])
        if key not in expected or key in lookup:raise ValueError('unexpected or duplicate quality pair')
        lookup[key]=row
    bad=[];unknown=lost=0
    for seed in seeds:
        affected=False
        for task in tasks:
            row=lookup.get((task,seed))
            if row is None or not row.get('complete_pair'):
                unknown+=1;affected=True
            elif row['baseline_success'] and not row['candidate_success']:
                lost+=1;affected=True
        if affected:bad.append(seed)
    upper=clopper_pearson(len(bad),len(seeds),.05)[1]
    return dict(seed_clusters=len(seeds),tasks=len(tasks),expected_pairs=len(expected),
        unknown_pairs=unknown,observed_lost_pairs=lost,bad_seed_count=len(bad),bad_seeds=bad,
        upper_bad_seed_probability=upper,success_difference_lower_bound=-upper,
        lower_bound_confidence=.975,margin=margin,supports_noninferiority=upper<margin,
        scope='fixed task/data split; independent evolution seed clusters; unknown pairs counted as losses',
        method='one-sided97.5% exact binomial bound on any-task success loss per seed')
