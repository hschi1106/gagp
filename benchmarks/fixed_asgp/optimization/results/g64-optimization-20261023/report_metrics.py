"""Closed quality coverage, gen0/final outcomes, and paired timing uncertainty."""
import itertools
import math

import study_statistics


def closed_quality_grid(jobs, tasks, seeds):
    expected = {(task, seed, variant) for task in tasks for seed in seeds for variant in ("baseline", "candidate")}
    if len(set(tasks)) != len(tasks) or len(set(seeds)) != len(seeds) or not expected:
        raise ValueError("Invalid frozen cohort")
    index = {}
    for job in jobs:
        key = (job["task"], job["seed"], job["variant"])
        if key not in expected or key in index:
            raise ValueError("Unexpected or duplicate quality search")
        if job.get("population", 1024) != 1024 or job.get("generations", 64) != 64:
            raise ValueError("Quality scope changed")
        index[key] = job
    return [dict(index[(task, seed, variant)]) if (task, seed, variant) in index else
            dict(task=task, seed=seed, variant=variant, population=1024, generations=64, status="missing")
            for seed in seeds for task in tasks for variant in ("baseline", "candidate")]


def champion_outcomes(common_train, common_test, native_train, native_test):
    """Call only after source/mapping/native fidelity validation, never status alone."""
    common = [d["history"] + [d["final"]] for d in (common_train, common_test)]
    native = [d["programs"] for d in (native_train, native_test)]
    if any(len(rows) != 65 for rows in (*common, *native)):
        raise ValueError("Need every gen0..63 champion and the generation64 final")
    joint = []
    for i in range(65):
        complete = True
        for scores, observations in zip(common, native):
            score, row = scores[i], observations[i]
            if row["program"] != i or row["cpu_cases"] != score["cases"] or row["cases"] != score["cases"]:
                raise ValueError("Champion/case alignment differs")
            complete &= (score["correct_count"] == score["cases"] and score["wrong"] == 0 and
                         score["runtime_errors"] == 0 and row["cpu_wrong"] == 0 and
                         row["cpu_errors"] == 0 and row["cpu_timeouts"] == 0)
        joint.append(bool(complete))
    return dict(initial_success=joint[0], final_success=joint[-1], newly_solved=not joint[0] and joint[-1],
                lost_initial=joint[0] and not joint[-1],
                first_joint_success_generation=next((i for i, ok in enumerate(joint) if ok), None),
                train_accuracy=common_train["final"]["exact_accuracy"], test_accuracy=common_test["final"]["exact_accuracy"],
                joint_success_history=joint,
                first_success_scope="Offline native CPU train+test judge of training-selected champions; not a test-guided stopping rule")


def quantile(values, p):
    values = sorted(values)
    if not values or not 0 <= p <= 1:
        raise ValueError("Invalid quantile")
    position = (len(values) - 1) * p
    low = int(position)
    high = min(low + 1, len(values) - 1)
    return values[low] + (values[high] - values[low]) * (position - low)


def paired_round_interval(ratios_by_task):
    """Enumerate the3^3 paired-round bootstrap; the fixed30 tasks stay fixed.

    All tasks from the same timing round are resampled together. The interval
    describes the three measured timing rounds, not new tasks or new genotypes.
    """
    if not ratios_by_task or any(len(rows) != 3 for rows in ratios_by_task):
        raise ValueError("Need all three paired repetitions for every fixed task")
    if any(not math.isfinite(v) or v <= 0 for rows in ratios_by_task for v in rows):
        raise ValueError("Invalid or missing speed ratio")
    logs = [[math.log(v) for v in rows] for rows in ratios_by_task]
    values = [math.exp(math.fsum(rows[i] for rows in logs for i in draw) / (3 * len(logs)))
              for draw in itertools.product(range(3), repeat=3)]
    return dict(geometric_mean=study_statistics.geometric_mean(v for rows in ratios_by_task for v in rows),
                timing_ci95=[quantile(values, .025), quantile(values, .975)], repetitions=3,
                fixed_tasks=len(logs), bootstrap_draws=len(values),
                method="Paired timing-round cluster bootstrap, all27 resamples, fixed task set and initial seed")


def secondary_display(pairs, cells, tasks):
    """Expose existing qualified P16384 ratios; never present a subset as all30."""
    expected = set(tasks)
    if not tasks or len(expected) != len(tasks):
        raise ValueError("Invalid secondary display task set")
    for records in (pairs, cells):
        if len(records) != len(tasks) or {r["task"] for r in records} != expected:
            raise ValueError("Need one secondary row per frozen task, including failures")
        if any(r["population"] != 16384 for r in records):
            raise ValueError("Secondary population differs")
    if any(r["repetition"] != 0 for r in pairs):
        raise ValueError("Secondary display requires the single frozen timing round")
    paired, qualified = ({r["task"]: r for r in records} for records in (pairs, cells))
    result = []
    for task in tasks:
        pair, cell = paired[task], qualified[task]
        speedup = cell["speedup"] if cell["qualified"] else None
        asgp = pair["asgp_over_candidate"] if cell["qualified"] else None
        for value in (speedup, asgp):
            if value is not None and (type(value) not in (int, float) or not math.isfinite(value) or value <= 0):
                raise ValueError("Invalid qualified secondary ratio")
        if cell["qualified"] and speedup is None:
            raise ValueError("Qualified secondary row has no ratio")
        result.append(dict(task=task, population=16384, generations=64, repetition=0,
                           speedup=speedup, asgp_over_candidate=asgp,
                           gagp_qualified=cell["qualified"], asgp_qualified=asgp is not None,
                           complete=cell["qualified"] and asgp is not None,
                           statuses=pair["statuses"], asgp_status=pair["asgp_status"]))
    asgp = [r["asgp_over_candidate"] for r in result if r["asgp_qualified"]]
    return result, dict(asgp_qualified_tasks=len(asgp), expected_tasks=len(tasks),
                        asgp_speedup=study_statistics.geometric_mean(asgp) if len(asgp) == len(tasks) else None)


def card_memory_summary(rows, population):
    """Summarize recorded whole-card samples, including unsuccessful searches.

    Stage records initialize a peak to zero even when no sample succeeded.
    A peak without a positive sample count is unknown, never zero usage.
    Search completion and native qualification remain separate evidence.
    """
    if any(row["population"] != population for row in rows):
        raise ValueError("VRAM cohort population differs")
    gpu = [row for row in rows if row["variant"] in ("baseline", "candidate")]
    sampled, errors = [], []
    for row in gpu:
        count, peak = row.get("memory_samples"), row.get("sampled_card_peak_bytes")
        if type(count) is int and count > 0 and type(peak) is int and peak >= 0:
            sampled.append(row)
        error = row.get("memory_sampling_errors")
        if type(error) is int and error >= 0:
            errors.append(error)
    return dict(population=population, expected_gpu_searches=len(gpu), sampled_gpu_searches=len(sampled),
                unknown_gpu_searches=len(gpu) - len(sampled),
                successful_gpu_searches=sum(row["search_status"] == "ok" for row in gpu),
                qualified_gpu_searches=sum(row["qualified"] is True for row in gpu),
                sampled_failed_searches=sum(row["search_status"] != "ok" for row in sampled),
                peak_bytes=max((row["sampled_card_peak_bytes"] for row in sampled), default=None),
                recorded_sampling_errors=sum(errors), unknown_sampling_error_searches=len(gpu) - len(errors),
                scope="Whole-card NVML device0, approximately200ms sampling; includes desktop and failed calls; "
                      "may miss short peaks. Recorded samples do not establish exact peak or universal capacity.")
