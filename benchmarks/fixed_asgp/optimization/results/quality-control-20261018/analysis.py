"""Paired descriptive inference within each fixed task; no tuning by test scores."""
import math
import random
import statistics


def wilson(successes, trials, z=1.959963984540054):
    if not trials:return None,None
    p=successes/trials;denominator=1+z*z/trials
    center=(p+z*z/(2*trials))/denominator
    half=z*math.sqrt(p*(1-p)/trials+z*z/(4*trials*trials))/denominator
    return max(0,center-half),min(1,center+half)


def mcnemar(a_only,b_only):
    n=a_only+b_only
    if not n:return 1.0
    return min(1.0,2*sum(math.comb(n,k) for k in range(min(a_only,b_only)+1))/2**n)


def holm(values):
    order=sorted(range(len(values)),key=values.__getitem__);result=[None]*len(values);previous=0
    for rank,index in enumerate(order):
        previous=max(previous,min(1,values[index]*(len(values)-rank)))
        result[index]=previous
    return result


def bootstrap(values,statistic=statistics.mean,repetitions=4000):
    if not values:return None,None
    rng=random.Random(20261018)
    samples=sorted(statistic([rng.choice(values) for unused in values]) for i in range(repetitions))
    return samples[int(.025*(repetitions-1))],samples[int(.975*(repetitions-1))]


def median(values):return statistics.median(values) if values else None

def geomean(values):return math.exp(statistics.mean(map(math.log,values))) if values else None
