"""Read immutable evidence one result at a time; keep unknowns in denominators."""
import json
import math
from pathlib import Path

import champion_audit
import common_audit
import full_population_audit
import quality
import quality_data
import report_costs
import report_metrics
import serial


class Evidence:
    def __init__(self):
        self.hashes = {}
        self.issues = []
        self.native_calls = 0
        self.native_observations = 0
        self.native_executed = 0
        self.judge_process_s = 0.0

    def verify(self, path, digest):
        path = str(Path(path).resolve())
        if path not in self.hashes:
            self.hashes[path] = serial.sha(path)
        assert self.hashes[path] == digest, f"Artifact hash changed: {path}"

    def artifact(self, job, field="output"):
        self.verify(job[field], job[field + "_sha256"])
        return serial.read(job[field])

    def job(self, job):
        assert job["status"] == "ok", "Job did not succeed"
        assert job["returncode"] == 0
        assert serial.START <= job["started_epoch"] <= job["finished_epoch"] <= serial.DEADLINE - 14400
        assert math.isfinite(job["process_s"]) and job["process_s"] > 0
        assert job.get("input_sha256"), "Missing input provenance"
        for path, digest in job["input_sha256"].items():
            self.verify(path, digest)
        for field in ("output", "export", "history", "raw_output", "mapping"):
            if field in job:
                self.verify(job[field], job[field + "_sha256"])

    def stage(self, path, freeze, variant, *, frozen=True):
        path = Path(path)
        if not path.exists():
            return {}
        doc = serial.read(path)
        self.verify(path.parent / "bench", freeze["variants"][variant]["binary_sha256"])
        assert doc["binary_sha256"] == freeze["variants"][variant]["binary_sha256"]
        assert doc["settings"] == freeze["settings"], "Stage global settings differ"
        if frozen:
            assert doc["freeze_sha256"] == freeze["_sha256"]
        return doc["jobs"]

    def native(self, job, search, split, count, asts, *, full=False):
        self.job(job)
        expanded = self.artifact(job)
        mapping = self.artifact(job, "mapping")
        unique, indices, digests = champion_audit.compress_programs(asts)
        assert mapping["observation_to_unique"] == indices
        assert mapping["unique_ast_sha256"] == digests
        prepared = serial.read(job["prepared"])
        assert prepared["programs"] == unique, "Native judge evaluated different ASTs"
        raw = self.artifact(job, "raw_output")
        assert expanded == champion_audit.expand_document(raw, indices), "Expanded CPU observations differ"
        result = (full_population_audit.validate(expanded, search, len(asts), count) if full else
                  champion_audit.validate(expanded, search, split, count))
        assert result["native_fidelity_pass"] is True and job["native_fidelity_pass"] is True
        assert result["native_mismatches"] == job["native_mismatches"]
        assert job["native_cpu_executed_programs"] == len(unique)
        self.native_calls += 1
        self.native_observations += len(asts)
        self.native_executed += len(unique)
        self.judge_process_s += job["process_s"]
        # Expanded native docs contain aggregates only; common case_results
        # are explicitly stripped below after validating every observation.
        return expanded

    def common(self, job, task, count, backend="gagp_cpu_1t"):
        self.job(job)
        doc = self.artifact(job)
        common_audit.validate(doc, count, task, 64, backend)
        self.judge_process_s += job["process_s"]
        return dict(cases=doc["cases"], cases_sha256=doc["cases_sha256"],
                    history=[{k: v for k, v in row.items() if k != "case_results"} for row in doc["history"]],
                    final={k: v for k, v in doc["final"].items() if k != "case_results"})

    def quality_run(self, job, judges, inputs, freeze):
        result = {k: job[k] for k in ("task", "seed", "variant", "status")}
        result.update(complete=False, initial_success=False, final_success=False, newly_solved=False)
        result.update(report_costs.post_search_audit({}, job["variant"]))
        if job["status"] != "ok":
            return result
        try:
            self.job(job)
            task, seed, variant = (job[k] for k in ("task", "seed", "variant"))
            count = inputs["metadata"]["train_count"]
            source = inputs["seeds"][str(seed)]
            for path, digest in ((source["source"], source["source_sha256"]),
                                 (source["prepared"], source["prepared_sha256"]),
                                 (inputs["grammar"], inputs["grammar_sha256"])):
                assert job["input_sha256"].get(path) == digest
            for key, value in freeze["variants"][variant]["extra"].items():
                assert job["extra"].get(key) == value
            search = self.artifact(job)
            quality.validate(search, count, 64)
            result.update(report_costs.post_search_audit(search, variant))
            exported = self.artifact(job, "export")
            history = self.artifact(job, "history")["history"]
            assert len(exported["programs"]) == 1 and len(history) == 64
            asts = [row["ast"] for row in history] + exported["programs"]
            common, native = {}, {}
            for split in ("train", "test"):
                prefix = f"{task}.s{seed}.{variant}.{split}"
                a, b = judges[prefix], judges[prefix + ".native"]
                expected_source = source["source"] if split == "train" else inputs["test_source"]
                assert a["input_sha256"][expected_source] == serial.sha(expected_source)
                assert serial.read(b["source"])["cases"] == serial.read(expected_source)["cases"], "Native judge case split differs"
                common[split] = self.common(a, task, inputs["metadata"][split + "_count"])
                native[split] = self.native(b, search, split, inputs["metadata"][split + "_count"], asts)
            result.update(report_metrics.champion_outcomes(common["train"], common["test"], native["train"], native["test"]))
            result.update(complete=True, search_s=search["evolve_wall_ms"] / 1000,
                          final_export_sha256=job["export_sha256"], history_sha256=job["history_sha256"],
                          train_cases_sha256=common["train"]["cases_sha256"], test_cases_sha256=common["test"]["cases_sha256"],
                          champion_ast_fitness=[(row["ast"], row["fitness"]) for row in history],
                          final_descriptors=search["final_population"],
                          train_accuracy_history=[r["exact_accuracy"] for r in common["train"]["history"]] + [common["train"]["final"]["exact_accuracy"]],
                          test_accuracy_history=[r["exact_accuracy"] for r in common["test"]["history"]] + [common["test"]["final"]["exact_accuracy"]])
        except (AssertionError, KeyError, ValueError, OSError) as error:
            result.update(status="evidence_invalid", reason=repr(error))
            self.issues.append(dict(task=job["task"], seed=job["seed"], variant=job["variant"], issue=repr(error)))
        return result


def quality_pair(a, b):
    complete = a["complete"] and b["complete"]
    equal = False
    if complete:
        equal = all(a[key] == b[key] for key in
                    ("champion_ast_fitness", "final_descriptors", "final_export_sha256", "train_cases_sha256", "test_cases_sha256"))
    row = dict(task=a["task"], seed=a["seed"], complete_pair=complete,
               baseline_success=a["final_success"], candidate_success=b["final_success"],
               trajectory_equal=equal, baseline_status=a["status"], candidate_status=b["status"])
    if complete:
        row["speedup"] = a["search_s"] / b["search_s"]
    return row


def validate_splits(inputs, freeze, evidence):
    rows = []
    for task in freeze["tasks"]:
        entry = inputs[task]
        if entry.get("status") != "prepared":
            rows.append(dict(task=task, status=entry.get("status", "missing")))
            continue
        evidence.verify(entry["split"], entry["split_sha256"])
        split = serial.read(entry["split"])
        train, test = split["train"], split["test"]
        assert len(train) == entry["metadata"]["train_count"] and len(test) == entry["metadata"]["test_count"]
        a, b = set(quality_data.helper.unique(train)), set(quality_data.helper.unique(test))
        assert a and b and not a & b, f"Overlapping train/test input: {task}"
        evidence.verify(entry["test_source"], entry["test_source_sha256"])
        test_source = serial.read(entry["test_source"])
        assert test_source["cases"] == test and test_source["quality_data_seed"] == freeze["data_seed"]
        identical = admitted = 0
        for seed in freeze["seeds"]:
            seed_entry = entry.get("seeds", {}).get(str(seed), {})
            if not seed_entry.get("source"):
                continue
            evidence.verify(seed_entry["source"], seed_entry["source_sha256"])
            source = serial.read(seed_entry["source"])
            assert source["cases"] == train, f"Training split differs by seed: {task}"
            assert source["population_seed"] == seed and source["quality_data_seed"] == freeze["data_seed"]
            evidence.verify(seed_entry["frozen_source"], seed_entry["frozen_source_sha256"])
            original = serial.read(seed_entry["frozen_source"])
            assert original["population"] == source["population"] and len(source["population"]) == 1024
            identical += 1
            admitted += seed_entry.get("status") == "ok"
        rows.append(dict(task=task, status="ok" if identical == len(freeze["seeds"]) else "partial_preparation",
                         train_count=len(train), test_count=len(test), input_disjoint=True,
                         identical_seed_count=identical, expected_seeds=len(freeze["seeds"]),
                         admitted_seed_count=admitted, original_population_draws_preserved=True,
                         metadata=entry["metadata"]))
    return rows


def collect_quality(name, inputs, freeze, evidence):
    record = serial.RAW / (name + "-controller.json")
    state = serial.read(record) if record.exists() else {}
    jobs = []
    judge_by_seed_variant = {}
    for seed, manifests in state.get("seed_manifests", {}).items():
        for variant in ("baseline", "candidate"):
            for kind in ("search", "judges"):
                stage_jobs = evidence.stage(manifests[variant + "_" + kind], freeze, variant)
                if kind == "search":
                    jobs.extend(stage_jobs.values())
                else:
                    judge_by_seed_variant[(int(seed), variant)] = stage_jobs
    jobs.extend(state.get("not_run_samples", []))
    closed = report_metrics.closed_quality_grid(jobs, freeze["tasks"], freeze["seeds"])
    rows, pairs = [], []
    for index in range(0, len(closed), 2):
        results = [evidence.quality_run(job, judge_by_seed_variant.get((job["seed"], job["variant"]), {}),
                                        inputs[job["task"]], freeze) for job in closed[index:index + 2]]
        pairs.append(quality_pair(*results))
        for row in results:
            # Keep65 scalar observations for plots, never full ASTs or large
            # case-result tables across the full cohort.
            row.pop("champion_ast_fitness", None)
            row.pop("final_descriptors", None)
            rows.append(row)
    return rows, pairs


def secondary_inputs(name, inputs, freeze, evidence):
    path = serial.RAW / (name + "-controller.json")
    state = serial.read(path) if path.exists() else {}
    result = []
    for task in freeze["tasks"]:
        entry = state.get("inputs", {}).get(task, {})
        row = dict(task=task, status=entry.get("status", "missing"), draw_verified=False)
        if entry.get("source"):
            evidence.verify(entry["source"], entry["source_sha256"])
            evidence.verify(entry["frozen_source"], entry["frozen_source_sha256"])
            source, original = serial.read(entry["source"]), serial.read(entry["frozen_source"])
            primary = serial.read(inputs[task]["seeds"][str(freeze["seed"])]["source"])
            assert source["population"] == original["population"] and len(source["population"]) == 16384
            assert source["population_seed"] == freeze["seed"] and source["quality_data_seed"] == freeze["data_seed"]
            assert source["cases"] == primary["cases"]
            equal = source["population"][:1024] == primary["population"]
            assert equal == entry["primary_prefix_equal"]
            row.update(draw_verified=True, primary_prefix_equal=equal, same_primary_training_split=True)
        result.append(row)
    return result
