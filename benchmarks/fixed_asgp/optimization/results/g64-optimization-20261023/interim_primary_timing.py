"""Lightweight closed-primary timing summary; full raw qualification stays pending."""
from argparse import ArgumentParser
import json
import math
from pathlib import Path
import time
import traceback

import report_metrics
import serial
import study_statistics as stats


def main(out):
    out.mkdir()  # Keep every earlier analysis attempt.
    state = dict(status="checking_closed_metadata", started_epoch=time.time(),
                 source_sha256={str(p): serial.sha(p) for p in
                     (Path(__file__), serial.BASE / "report_metrics.py", serial.BASE / "study_statistics.py")},
                 scope="Interim controller-metadata timing only; no raw search/AST/case outputs or GPU work")
    sources = {}

    def read(path):
        path = Path(path)
        data = path.read_bytes()
        import hashlib
        sources[str(path)] = hashlib.sha256(data).hexdigest()
        return json.loads(data)

    try:
        freeze_path = serial.RAW / "formal0-speed-freeze.json"
        freeze = read(freeze_path)
        assert freeze["population"] == 1024 and freeze["generations"] == 64 and freeze["repetitions"] == 3
        assert freeze["tasks"] == list(serial.PAPER_TASKS)
        for name in ("formal0-speed", "formal0-audits"):
            controller = read(serial.RAW / (name + "-controller.json"))
            assert controller["status"] == "requested_phase_complete"
            assert not Path(f"/proc/{controller['pid']}").exists()
        stages = {}
        for variant in ("baseline", "candidate", "asgp"):
            stage = read(serial.RAW / ("formal0-speed-" + variant) / "manifest.json")
            assert stage["status"] == "requested_phase_complete"
            assert stage["freeze_sha256"] == sources[str(freeze_path)]
            assert stage["settings"] == freeze["settings"]
            assert stage["binary_sha256"] == freeze["variants"][variant]["binary_sha256"]
            expected = {f"{t}.s{freeze['seed']}.r{r}.{variant}" for t in freeze["tasks"] for r in range(3)}
            assert set(stage["jobs"]) == expected
            for job in stage["jobs"].values():
                assert job["status"] == "ok" and job["returncode"] == 0
                assert job["population"] == 1024 and job["generations"] == 64 and job["seed"] == freeze["seed"]
                assert serial.START <= job["started_epoch"] <= job["finished_epoch"] <= serial.DEADLINE - 14400
                assert math.isfinite(job["search_s"]) and job["search_s"] > 0
            stages[variant] = stage["jobs"]
        rows = read(serial.RAW / "formal0-speed-comparisons.json")
        assert len(rows) == 90 and {(r["task"], r["repetition"]) for r in rows} == {
            (t, r) for t in freeze["tasks"] for r in range(3)}
        for row in rows:
            jobs = {v: stages[v][f"{row['task']}.s{freeze['seed']}.r{row['repetition']}.{v}"] for v in stages}
            assert row["statuses"] == ["ok", "ok"] and row["asgp_status"] == "ok"
            assert all(row[k] is True for k in ("final_descriptors_equal", "champion_ast_fitness_equal", "final_export_equal"))
            assert len({job["cases"] for job in jobs.values()}) == 1
            # The first two measure arguments are the same frozen source/prepared
            # population for each variant; grammar is the following argument.
            for index in (2, 3, 4):
                assert len({job["command"][index] for job in jobs.values()}) == 1
                path = jobs["baseline"]["command"][index]
                assert len({job["input_sha256"][path] for job in jobs.values()}) == 1
            for field, numerator, denominator in (("speedup", "baseline", "candidate"),
                    ("asgp_over_candidate", "asgp", "candidate"), ("asgp_over_baseline", "asgp", "baseline")):
                ratio = jobs[numerator]["search_s"] / jobs[denominator]["search_s"]
                assert math.isclose(row[field], ratio, rel_tol=1e-12)
        audit_count, native_count = 0, 0
        for variant in ("baseline", "candidate", "asgp"):
            stage = read(serial.RAW / ("formal0-audits-" + variant) / "manifest.json")
            assert stage["status"] == "requested_phase_complete" and stage["settings"] == freeze["settings"]
            assert stage["binary_sha256"] == freeze["variants"][variant]["binary_sha256"]
            assert all(job["status"] == "ok" and job["returncode"] == 0 for job in stage["jobs"].values())
            audit_count += len(stage["jobs"])
            for job in stage["jobs"].values():
                if "native_fidelity_pass" in job:
                    assert job["native_fidelity_pass"] is True
                    assert all(type(v) is int and v == 0 for v in job["native_mismatches"].values())
                    native_count += 1
        assert audit_count == 420 and native_count == 240
        grouped = stats.speed_summary(rows, freeze["tasks"], [1024], 3)
        primary = grouped["groups"]["1024"]
        assert primary["full_coverage"]
        interval = report_metrics.paired_round_interval([[r["speedup"] for r in rows if r["task"] == task]
                                                       for task in freeze["tasks"]])
        result = dict(population=1024, generations=64, tasks=30, repetitions=3, paired_observations=90,
                      previous_gagp_over_candidate=primary["observed_geometric_mean"], timing_ci95=interval["timing_ci95"],
                      asgp1t_over_candidate=stats.geometric_mean(r["asgp_over_candidate"] for r in rows),
                      asgp1t_over_previous_gagp=stats.geometric_mean(r["asgp_over_baseline"] for r in rows),
                      target_observed_in_timing=primary["target_reached"], controller_audit_jobs=audit_count,
                      controller_native_pass_jobs=native_count, source_metadata_sha256=sources,
                      qualification="Interim timing from closed controller metadata; full independent raw/source/binary/input/AST mapping revalidation pending",
                      scope="Fixed engine-search wall; ASGP includes different setup/history scope and native operators/source phase limits. No paper replication/equivalence claim.",
                      objective_achieved=False, no_new_search=True)
        with (out / "result.json").open("x") as stream:
            json.dump(result, stream, indent=2)
            stream.write("\n")
        state.update(status="interim_timing_ready_raw_recheck_pending", finished_epoch=time.time())
        print(json.dumps({k: result[k] for k in ("previous_gagp_over_candidate", "timing_ci95", "asgp1t_over_candidate",
                                                "target_observed_in_timing", "qualification")}))
    except BaseException as error:
        state.update(status="failed", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        state["source_metadata_sha256"] = sources
        serial.save(out / "manifest.json", state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    main(parser.parse_args().out.resolve())
