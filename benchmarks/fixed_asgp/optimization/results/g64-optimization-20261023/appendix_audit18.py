"""Audit completed OLD TRAINING appendix evidence; never launch searches.

Physical work already reflects shape/integer-case caches. Exact grouping saves
at most N*logical_cases, not necessarily exactly that many physical cases.
"""
import argparse
import csv
import math
from pathlib import Path
import re
import time

import serial


VARIANTS = ("baseline", "selected_control", "ordinary", "current_groups")
SCALARS = ("best", "best_wrong", "best_nodes", "mean", "errors", "timeouts",
           "cases", "expected_program_cases", "unscored")


def physical_savings_valid(before, after, grouped, logical_cases):
    return 0 <= before - after <= grouped * logical_cases


def run(out):
    out = Path(out)
    if out.exists():
        raise ValueError("Preserve previous analysis; use a NEW output path")
    if time.time() >= serial.DEADLINE:
        raise ValueError("Original hard deadline reached")
    import fcntl
    with serial.GPU_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        protocol = serial.read(serial.BASE / "protocol.json")
        names = ("packed-integration15", "packed-confirm15", "current-groups17", "current-groups-confirm17")
        controllers = {n: serial.read(serial.RAW / (n + "-controller.json")) for n in names}
        for name, state in controllers.items():
            assert not Path(f"/proc/{state['pid']}").exists(), name
            for path, digest in state.get("harness_sha256", {}).items():
                assert serial.sha(path) == digest, path
        wait = serial.read(serial.RAW / "current-groups17-wait-controller.json")
        for path, digest in wait["harness_sha256"].items():
            assert serial.sha(path) == digest, path
        for spec in protocol["development_inputs"].values():
            for path, digest in spec["sha256"].items():
                assert serial.sha(path) == digest, path
        source = serial.read(serial.RAW / "current-groups17/source-manifest.json")
        for field, root in (("source_sha256", Path(source["root"])), ("original_source_sha256", serial.ROOT)):
            for relative, digest in source[field].items():
                assert serial.sha(root / relative) == digest, relative
        stages = {v: serial.read(serial.RAW / ("current-groups17-" + v) / "manifest.json") for v in VARIANTS}
        rows, issues = [], []
        for task in protocol["tasks"]:
            docs = {v: serial.read(s["jobs"][task + ".s0.g64"]["output"]) for v, s in stages.items()}
            reference = [[g[k] for k in SCALARS] for g in docs["selected_control"]["generations"]]
            equal = all([[g[k] for k in SCALARS] for g in d["generations"]] == reference for d in docs.values())
            if not equal:
                issues.append(dict(task=task, type="generation_scalar_trajectory_mismatch"))
            ordinary, group = docs["ordinary"], docs["current_groups"]
            counts = []
            for a, g in zip(ordinary["generations"], group["generations"]):
                match = re.search(r"-current-fitness-groups(\d+)(?:-|$)", g["execution_profile"])
                count = int(match[1]) if match else 0
                if not physical_savings_valid(a["physical_program_cases"], g["physical_program_cases"], count, group["cases"]):
                    issues.append(dict(task=task, generation=g["generation"], type="physical_savings_outside_bounds"))
                if a["fitness_reused_program_cases"] != g["fitness_reused_program_cases"]:
                    issues.append(dict(task=task, generation=g["generation"], type="previous_parent_reuse_changed"))
                counts.append(count)
            row = dict(task=task, generation_scalars_equal=equal,
                       group_active_generations=sum(n > 0 for n in counts),
                       grouped_program_evaluations=sum(counts),
                       physical_cases_ordinary=sum(g["physical_program_cases"] for g in ordinary["generations"]),
                       physical_cases_groups=sum(g["physical_program_cases"] for g in group["generations"]))
            for variant, document in docs.items():
                job = stages[variant]["jobs"][task + ".s0.g64"]
                assert job["started_epoch"] >= controllers["current-groups17"]["started_epoch"]
                assert job["finished_epoch"] <= protocol["timed_cutoff_epoch"]
                assert [g["generation"] for g in document["generations"]] == list(range(64))
                assert document["cpu_audit_mismatches"] == document["cpu_audit_wrong_mismatches"] == 0
                for field in ("output", "export", "history"):
                    assert serial.sha(job[field]) == job[field + "_sha256"], job[field]
                row[variant + "_search_s"] = job["search_s"]
                row[variant + "_process_s"] = job["process_s"]
                row[variant + "_cpu_audit_s"] = document["cpu_audit_ms"] / 1000
                row[variant + "_cpu_audited_programs"] = document["cpu_audit_program_count"]
                row[variant + "_in_search_cpu_verified_programs"] = document["verification"]["programs"]
                row[variant + "_in_search_cpu_corrections"] = sum(document["verification"][k] for k in ("fitness_corrections", "wrong_corrections", "best_changes"))
                row[variant + "_sampled_card_peak_bytes"] = job.get("sampled_card_peak_bytes") if job.get("memory_samples", 0) > 0 else None
                row[variant + "_memory_samples"] = job.get("memory_samples", 0)
                row[variant + "_memory_sampling_errors"] = job.get("memory_sampling_errors", 0)
                row[variant + "_eval_s"] = job["components_s"]["eval_ms"]
                row[variant + "_repro_s"] = job["components_s"]["repro_ms"]
            row["groups_vs_same_binary_off"] = row["ordinary_search_s"] / row["current_groups_search_s"]
            row["physical_cases_saved_fraction"] = 1 - row["physical_cases_groups"] / row["physical_cases_ordinary"]
            rows.append(row)
        summary = dict(scope="OLD TRAINING P1024/G64/seed0/R1 only; no new heldout or 65champion native judges",
                       controllers={n: d["status"] for n, d in controllers.items()},
                       captured_harness_source_input_and_artifact_hashes_match=True,
                       all_searches_before_original_timing_cutoff=True,
                       all_generation_scalar_trajectories_equal=all(r["generation_scalars_equal"] for r in rows),
                       scalar_fields=SCALARS, group_active_generations=sum(r["group_active_generations"] for r in rows),
                       total_group_generations=30 * 64, grouped_program_evaluations=sum(r["grouped_program_evaluations"] for r in rows),
                       physical_cases_ordinary=sum(r["physical_cases_ordinary"] for r in rows),
                       physical_cases_groups=sum(r["physical_cases_groups"] for r in rows),
                       groups_vs_same_binary_off_gm=math.exp(sum(math.log(r["groups_vs_same_binary_off"]) for r in rows) / 30),
                       cpu_audit_program_observations=sum(r[v + "_cpu_audited_programs"] for r in rows for v in VARIANTS),
                       in_search_cpu_verified_programs=sum(r[v + "_in_search_cpu_verified_programs"] for r in rows for v in VARIANTS),
                       in_search_cpu_corrections=sum(r[v + "_in_search_cpu_corrections"] for r in rows for v in VARIANTS),
                       new_65champion_cpu_audits=0, new_heldout_evaluations=0, issues=issues,
                       analysis_source_sha256=serial.sha(Path(__file__)), completed_epoch=time.time())
        summary["physical_cases_saved_fraction"] = 1 - summary["physical_cases_groups"] / summary["physical_cases_ordinary"]
        summary["post_search_cpu_audit_section_s"] = sum(r[v + "_cpu_audit_s"] for r in rows for v in VARIANTS)
        summary["totals_by_arm"] = {v: {field: sum(r[v + "_" + field] for r in rows) for field in ("search_s", "process_s", "eval_s", "repro_s", "cpu_audit_s")} for v in VARIANTS}
        out.mkdir()
        serial.save(out / "results.json", dict(summary=summary, tasks=rows))
        with (out / "mechanism.csv").open("x", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator="\n")
            writer.writeheader(); writer.writerows(rows)
        return summary


if __name__ == "__main__":
    import json
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    print(json.dumps(run(parser.parse_args().out), indent=2))
