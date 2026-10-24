"""Pre-heldout full-grid cost forecast; failed/censored costs are never zero."""
import math

import serial


def choose_scale(now, costs, seed_counts=(80, 40, 20, 10), safety=1.35):
    required = ("quality_prepare_seed_s", "quality_search_seed_s", "quality_judge_seed_s",
                "speed_search_s", "speed_audit_s", "secondary_prepare_s", "secondary_search_s",
                "secondary_audit_s", "source_checks_s", "fixed_s")
    if not math.isfinite(now) or now < serial.START or now >= serial.DEADLINE:
        raise ValueError("Forecast clock is outside the original window")
    if tuple(seed_counts) != (80, 40, 20, 10) or safety != 1.35:
        raise ValueError("Keep the pre-heldout scale policy")
    if any(k not in costs or type(costs[k]) not in (int, float) or not math.isfinite(costs[k]) or costs[k] <= 0 for k in required):
        raise ValueError("Every complete-grid cost must be observed or explicitly estimated, finite and positive")
    available = serial.DEADLINE - 14400 - now
    per_seed = sum(costs[k] for k in required[:3])
    fixed = sum(costs[k] for k in required[3:])
    plans = [dict(seed_count=n, quality_s=n * per_seed, fixed_grid_s=fixed,
                  expected_s=n * per_seed + fixed,
                  guarded_s=safety * (n * per_seed + fixed),
                  fits=safety * (n * per_seed + fixed) <= available) for n in seed_counts]
    return dict(started_epoch=serial.START, deadline_epoch=serial.DEADLINE, forecast_epoch=now,
                timed_cutoff_epoch=serial.DEADLINE - 14400, report_reserve_s=14400,
                available_s=available, safety=safety, costs=costs, plans=plans,
                selected_seed_count=next((p["seed_count"] for p in plans if p["fits"]), None),
                scope="All30 P1024/G64/R3 plus fresh ASGP1T; all30 P16384/G64/R1 secondary; no scope reduction")


def complete_jobs(path, tasks, *, native=False):
    manifest = serial.read(path)
    assert manifest["status"] == "requested_phase_complete", "Cost phase is incomplete"
    jobs = list(manifest["jobs"].values())
    assert len(jobs) == len(tasks) and {j["task"] for j in jobs} == set(tasks)
    assert all(j["status"] == "ok" and math.isfinite(j["process_s"]) and j["process_s"] > 0 for j in jobs)
    if native:
        assert all(j.get("native_fidelity_pass") is True for j in jobs), "Observed CPU fidelity failure blocks forecast"
    return {j["task"]: j for j in jobs}


def measured_costs(development_name, confirmation_name):
    """Read only old TRAINING timing/parity; caller holds the common GPU lock."""
    protocol = serial.read(serial.BASE / "protocol.json")
    tasks = protocol["tasks"]
    baseline = complete_jobs(serial.RAW / (development_name + "-baseline/manifest.json"), tasks)
    candidate = complete_jobs(serial.RAW / (development_name + "-candidate/manifest.json"), tasks)
    asgp = complete_jobs(serial.RAW / "asgp-calibration0-search/manifest.json", tasks)
    full = complete_jobs(serial.RAW / "full-audit-cost0/manifest.json", tasks, native=True)
    asgp_native = complete_jobs(serial.RAW / "asgp-calibration0-native/manifest.json", tasks, native=True)
    quality = serial.read(serial.RAW / "quality-cost0-controller.json")
    assert quality["status"] == "requested_phase_complete" and quality["full_cost_observed"] is True
    observed = {r["task"]: r for r in quality["observations"]}
    assert len(quality["observations"]) == len(observed) == 30 and set(observed) == set(tasks)
    preparation = serial.read(serial.RAW / "prepare-calibration0-controller.json")
    assert preparation["status"] == "requested_phase_complete" and preparation["tasks"] == 30
    confirm = serial.read(serial.RAW / (confirmation_name + "-controller.json"))
    assert confirm["status"] == "requested_phase_complete" and confirm["comparisons"] == 90
    # Use the slowest complete old seed cohort, including output and process
    # overhead. Formal quality uses smaller best-only exports, so this does not
    # count those savings in advance.
    search_seed_costs = [sum(j["process_s"] for j in (*baseline.values(), *candidate.values()))]
    extra_native = {}
    for variant in ("baseline", "candidate"):
        m = serial.read(serial.RAW / (confirmation_name + "-" + variant) / "manifest.json")
        assert m["status"] == "requested_phase_complete" and len(m["jobs"]) == 60
        for seed in (1, 2):
            jobs = [j for j in m["jobs"].values() if j["seed"] == seed]
            assert len(jobs) == 30 and {j["task"] for j in jobs} == set(tasks)
            assert all(j["status"] == "ok" for j in jobs)
        native = serial.read(serial.RAW / (confirmation_name + "-" + variant + "-native") / "manifest.json")
        assert native["status"] == "requested_phase_complete" and len(native["jobs"]) == 60
        assert all(j["status"] == "ok" and j.get("native_fidelity_pass") is True for j in native["jobs"].values())
        extra_native[variant] = list(native["jobs"].values())
    for seed in (1, 2):
        total = 0
        for variant in ("baseline", "candidate"):
            m = serial.read(serial.RAW / (confirmation_name + "-" + variant) / "manifest.json")
            total += sum(j["process_s"] for j in m["jobs"].values() if j["seed"] == seed)
        search_seed_costs.append(total)
    gagp_judges = asgp_judges = 0
    for task in tasks:
        row = observed[task]
        assert row["train_count"] == protocol["development_inputs"][task]["train_count"]
        ratio = 1 + row["forecast_test_count"] / row["train_count"]
        costs = row["costs"]
        assert all(c["status"] == "ok" and c["process_s"] > 0 for c in costs.values())
        assert costs["gagp_native"]["native_fidelity_pass"] is True
        native_s = max([costs["gagp_native"]["process_s"]] +
                       [j["process_s"] for jobs in extra_native.values() for j in jobs if j["task"] == task])
        gagp_judges += (costs["gagp_common"]["process_s"] + native_s) * ratio
        asgp_judges += (costs["asgp_common"]["process_s"] + asgp_native[task]["process_s"]) * ratio
    gpu_search = sum(j["process_s"] for j in (*baseline.values(), *candidate.values()))
    asgp_search = sum(j["process_s"] for j in asgp.values())
    full_s = sum(j["process_s"] for j in full.values())
    primary_audits = 2 * full_s + 2 * gagp_judges + asgp_judges
    result = dict(quality_prepare_seed_s=preparation["one_seed_cohort_process_s"],
                  quality_search_seed_s=max(search_seed_costs), quality_judge_seed_s=2 * gagp_judges,
                  speed_search_s=3 * (gpu_search + asgp_search), speed_audit_s=primary_audits,
                  secondary_prepare_s=16 * preparation["one_seed_cohort_process_s"],
                  secondary_search_s=16 * (gpu_search + asgp_search),
                  secondary_audit_s=32 * full_s + 2 * (2 * gagp_judges + asgp_judges),
                  source_checks_s=3600, fixed_s=900)
    return result, dict(development=development_name, confirmation=confirmation_name,
                       old_seed_search_process_s=search_seed_costs,
                       assumptions=["P16384 preparation/search/full-population audit scale conservatively by population ratio16",
                                    "P16384 selected-champion judges budget2x P1024, same G64/cases",
                                    "Test cost scales from old TRAINING by old split case-count metadata; no test labels used",
                                    "1.35 safety is a runtime forecast, not a hard completion guarantee",
                                    "No calibration CPU results or ASGP timings reused as formal references/denominators"])
