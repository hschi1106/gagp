"""Global scale choice from training-runtime forecasts, before any fresh data."""
import math


def choose_scale(available_s,speed_s,quality_per_seed_s,fixed_s=600,safety=1.35):
    """Prioritize80 seed clusters; choose one common speed G by time only.

    All quality searches use G64. The speed matrix has two cold repetitions at
    both populations. Do not use measured speedup, test accuracy, solved tasks
    or task-specific settings to choose this scale.
    """
    if set(speed_s)!={8,16,32}:raise ValueError('complete global speed cost forecasts required')
    values=[available_s,quality_per_seed_s,fixed_s,safety,*speed_s.values()]
    if any(not math.isfinite(v) for v in values) or quality_per_seed_s<=0 or fixed_s<0 or safety<1:
        raise ValueError('invalid runtime budget or forecast')
    if any(v<=0 for v in speed_s.values()):raise ValueError('speed costs must be positive')
    for seeds in (80,40,20,10):
        for generations in (32,16,8):
            raw=speed_s[generations]+seeds*quality_per_seed_s
            cost=fixed_s+safety*raw
            if cost<=available_s:
                return dict(speed_generations=generations,speed_repetitions=2,
                    quality_generations=64,quality_seed_count=seeds,
                    quality_seeds=list(range(25,25+seeds)),estimated_total_s=cost,
                    raw_estimated_s=raw,available_s=available_s,safety_factor=safety,
                    fixed_preparation_floor_s=fixed_s,
                    rule='prioritize80/40/20/10seed clusters, then common speedG32/16/8, runtime only; all30bothP, R2, qualityG64')
    return None
