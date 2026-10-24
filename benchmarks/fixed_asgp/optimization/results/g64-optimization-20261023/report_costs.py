"""Expose recorded audit costs without changing search ratios or qualification.

Post-search cpu_audit_ms covers case preparation, genome identities and top16
compilation/evaluation. Offline process wall time also includes native GPU work;
neither field is a measurement of pure CPU evaluation time.
"""
import math
from pathlib import Path

import serial


def in_search_verification(search, variant):
    result = dict(in_search_cpu_verification_known=False, in_search_cpu_verification_s=None,
                  in_search_cpu_verified_programs=None, in_search_cpu_fitness_corrections=None,
                  in_search_cpu_wrong_corrections=None, in_search_cpu_best_changes=None)
    if variant == "asgp" or "verification" not in search:
        return result
    data = search["verification"]
    names = dict(programs="in_search_cpu_verified_programs",
                 fitness_corrections="in_search_cpu_fitness_corrections",
                 wrong_corrections="in_search_cpu_wrong_corrections", best_changes="in_search_cpu_best_changes")
    for source, target in names.items():
        value = data[source]
        if type(value) is not int or value < 0:
            raise ValueError("Invalid in-search verification count")
        result[target] = value
    elapsed = data["wall_ms"]
    if type(elapsed) not in (int, float) or not math.isfinite(elapsed) or elapsed < 0:
        raise ValueError("Invalid in-search verification time")
    result.update(in_search_cpu_verification_known=True, in_search_cpu_verification_s=elapsed / 1000)
    return result


def post_search_audit(search, variant):
    result = dict(post_search_cpu_audit_s=None, post_search_cpu_audit_programs=None,
                  post_search_cpu_audit_known=False)
    result.update(in_search_verification(search, variant))
    if variant == "asgp":
        return result
    if variant not in ("baseline", "candidate"):
        raise ValueError("Unknown search variant")
    if "cpu_audit_ms" not in search:
        return result
    elapsed, count = search["cpu_audit_ms"], search["cpu_audit_program_count"]
    if type(elapsed) not in (int, float) or not math.isfinite(elapsed) or elapsed < 0:
        raise ValueError("Invalid post-search audit duration")
    if type(count) is not int or count < 0:
        raise ValueError("Invalid post-search audit program count")
    result.update(post_search_cpu_audit_s=elapsed / 1000,
                  post_search_cpu_audit_programs=count, post_search_cpu_audit_known=True)
    return result


def offline_rows(stages):
    rows, seen = [], set()
    for stage in stages:
        for tag, job in stage["jobs"].items():
            key = stage["stage"], tag
            if key in seen:
                raise ValueError("Duplicate audit call would double-count its cost")
            seen.add(key)
            elapsed = job.get("process_s")
            valid = type(elapsed) in (int, float) and math.isfinite(elapsed) and elapsed >= 0
            rows.append(dict(stage=stage["stage"], cohort=stage["cohort"], variant=stage["variant"],
                             tag=tag, status=job["status"],
                             kind="native" if tag.endswith(".native") or ".full-native" in tag else "common",
                             started="started_epoch" in job, process_s=elapsed if valid else None,
                             recorded_cost_known=valid,
                             declared_native_unique_programs=job.get("native_cpu_executed_programs"),
                             declared_native_logical_observations=job.get("native_program_observations")))
    return rows


def summarize(search_cohorts, offline):
    gpu = [row for rows in search_cohorts.values() for row in rows if row["variant"] != "asgp"]
    known = [row for row in gpu if row.get("post_search_cpu_audit_known")]
    verified = [row for row in gpu if row.get("in_search_cpu_verification_known")]
    result = dict(post_search_cpu_audit_recorded_s=math.fsum(row["post_search_cpu_audit_s"] for row in known),
                post_search_cpu_audit_known=len(known), post_search_cpu_audit_expected=len(gpu),
                in_search_cpu_verification_known=len(verified),
                in_search_cpu_verification_s=math.fsum(row["in_search_cpu_verification_s"] for row in verified) if verified else None,
                offline_recorded_process_s=math.fsum(row["process_s"] for row in offline if row["recorded_cost_known"]),
                offline_recorded_calls=len(offline),
                offline_unknown_started_costs=sum(row["started"] and not row["recorded_cost_known"] for row in offline),
                offline_failed_recorded_process_s=math.fsum(row["process_s"] for row in offline
                    if row["recorded_cost_known"] and row["status"] != "ok"),
                audit_cost_scope="Recorded costs only, including failed calls; missing costs are unknown, not zero. "
                      "Post-search section includes preparation/identity/top16 compile+CPU evaluation; "
                      "offline calls are process wall time with GPU/native/common work, not pure CPU time. "
                      "In-search CPU verification remains inside measured evolve_wall_ms; "
                      "post-search/offline costs are separate. No cost is used to select settings.")
    for field in ("in_search_cpu_verified_programs", "in_search_cpu_fitness_corrections",
                  "in_search_cpu_wrong_corrections", "in_search_cpu_best_changes"):
        result[field] = sum(row[field] for row in verified) if verified else None
    return result


def collect_offline(formal_name, quality, speed, secondary, evidence):
    planned = []
    for variant in ("baseline", "candidate", "asgp"):
        planned.append(("primary", variant, serial.RAW / (formal_name + "-audits-" + variant) / "manifest.json", speed))
        planned.append(("secondary", variant, serial.RAW / (formal_name + "-secondary-" + variant + "-audits") / "manifest.json", secondary))
    controller = serial.RAW / (formal_name + "-quality-controller.json")
    state = serial.read(controller) if controller.exists() else {}
    assert all(int(seed) in quality["seeds"] for seed in state.get("seed_manifests", {}))
    for seed in quality["seeds"]:
        manifests = state.get("seed_manifests", {}).get(str(seed), {})
        for variant in ("baseline", "candidate"):
            expected = serial.RAW / (formal_name + f"-quality.s{seed}-" + variant + "-judges") / "manifest.json"
            planned.append(("quality", variant, Path(manifests.get(variant + "_judges", expected)), quality))
    stages, sources, missing = [], {}, []
    for cohort, variant, path, freeze in planned:
        if not path.exists():
            missing.append(str(path))
            continue
        stages.append(dict(stage=str(path.parent), cohort=cohort, variant=variant,
                           jobs=evidence.stage(path, freeze, variant, frozen=False)))
        sources[str(path)] = serial.sha(path)
    return offline_rows(stages), dict(manifest_sha256=sources, missing_manifests=missing)
