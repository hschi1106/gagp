"""Recompute fixed-grid speed ratios and qualify them with actual native audits."""
import math

import asgp_audit
import champion_audit
import report_costs
import secondary
import serial
import speed


def selected_trajectory(history, variant):
    key = "program" if variant == "asgp" else "ast"
    return [(row.get("generation", i), row[key], row["fitness"]) for i, row in enumerate(history["history"])]


def asgp_native(evidence, job, search, split, count, observations):
    evidence.job(job)
    expanded = evidence.artifact(job)
    raw = evidence.artifact(job, "raw_output")
    mapping = evidence.artifact(job, "mapping")
    unique, indices, hashes = champion_audit.compress_programs(observations)
    assert mapping["observation_to_unique"] == indices and mapping["unique_ast_sha256"] == hashes
    assert serial.read(job["source"])["population"] == unique
    expected = [dict(raw["programs"][i], program=p, source_unique_program=i) for p, i in enumerate(indices)]
    assert expanded["programs"] == expected
    result = asgp_audit.validate(expected, search, split, count)
    assert result["native_fidelity_pass"] is True and job["native_fidelity_pass"] is True
    assert result["native_mismatches"] == job["native_mismatches"]
    evidence.native_calls += 1
    evidence.native_observations += len(observations)
    evidence.native_executed += len(unique)
    evidence.judge_process_s += job["process_s"]
    return expanded


def audit_r0(evidence, jobs, job, data, freeze, inputs):
    task, seed, variant = (job[k] for k in ("task", "seed", "variant"))
    exported = evidence.artifact(job, "export")
    history = evidence.artifact(job, "history")["history"]
    if variant == "asgp":
        observations = [row["program"] for row in history] + [exported["best_program"]]
    else:
        assert len(exported["programs"]) == freeze["population"]
        observations = [row["ast"] for row in history] + [exported["programs"][0]]
        full_tag = f"{task}.p{freeze['population']}.{variant}.final.full-native"
        assert serial.read(jobs[full_tag]["source"])["cases"] == serial.read(inputs["seeds"][str(seed)]["source"])["cases"]
        evidence.native(jobs[full_tag], data, "train", data["cases"], exported["programs"], full=True)
    outcomes = {}
    for split in ("train", "test"):
        count = inputs["metadata"][split + "_count"]
        common_tag = f"{task}.s{seed}.r0.{variant}.{split}.common"
        native_tag = f"{task}.s{seed}.r0.{variant}.{split}.native" if variant == "asgp" else f"{task}.s{seed}.{variant}.{split}.native"
        expected_source = inputs["seeds"][str(seed)]["source"] if split == "train" else inputs["test_source"]
        cases = serial.read(expected_source)["cases"]
        assert serial.read(jobs[common_tag]["command"][2])["cases"] == cases
        assert serial.read(jobs[native_tag]["source"])["cases"] == cases
        backend = "asgp_1t" if variant == "asgp" else "gagp_cpu_1t"
        common = evidence.common(jobs[common_tag], task, count, backend)
        if variant == "asgp":
            native = asgp_native(evidence, jobs[native_tag], data, split, count, observations)
            selected = [r["wrong"] == 0 for r in native["programs"]]
        else:
            native = evidence.native(jobs[native_tag], data, split, count, observations)
            selected = [r["cpu_wrong"] == r["cpu_errors"] == r["cpu_timeouts"] == 0 for r in native["programs"]]
        scores = common["history"] + [common["final"]]
        outcomes[split] = [cpu_ok and score["correct_count"] == count and score["runtime_errors"] == 0
                           for cpu_ok, score in zip(selected, scores)]
    joint = [a and b for a, b in zip(outcomes["train"], outcomes["test"])]
    assert len(joint) == 65
    return dict(initial_success=joint[0], final_success=joint[-1], newly_solved=not joint[0] and joint[-1])


def collect(name, audit_name, freeze, inputs, evidence):
    population = freeze["population"]
    repeats = freeze.get("repetitions", 1)
    secondary_scope = population == 16384
    controller_path = serial.RAW / (name + "-controller.json")
    secondary_inputs = serial.read(controller_path).get("inputs", {}) if secondary_scope and controller_path.exists() else {}
    searches = {v: evidence.stage(serial.RAW / (name + "-" + v) / "manifest.json", freeze, v)
                for v in freeze["variants"]}
    audits = {v: evidence.stage(serial.RAW / ((name + "-" + v + "-audits") if secondary_scope else (audit_name + "-" + v)) / "manifest.json", freeze, v, frozen=False)
              for v in freeze["variants"]}
    rows, audit_rows, successes = [], [], {}
    r0_references = {}
    for repetition in range(repeats):
        for task in freeze["tasks"]:
            loaded, metadata = {}, {}
            for variant in freeze["variants"]:
                tag = f"{task}.s{freeze['seed']}.r{repetition}.{variant}"
                job = searches[variant].get(tag, dict(status="missing", task=task, variant=variant,
                                                     seed=freeze["seed"], population=population, repetition=repetition))
                record = dict(task=task, variant=variant, repetition=repetition, status=job["status"], search_status=job["status"],
                              qualified=False, population=population, search_s=None,
                              **report_costs.post_search_audit({}, variant),
                              process_s=job.get("process_s"), sampled_card_peak_bytes=job.get("sampled_card_peak_bytes"),
                              memory_samples=job.get("memory_samples"), memory_sampling_errors=job.get("memory_sampling_errors"),
                              memory_sampling=job.get("memory_sampling"))
                if job["status"] == "ok":
                    try:
                        evidence.job(job)
                        entry = secondary_inputs[task] if secondary_scope else inputs[task]["seeds"][str(freeze["seed"])]
                        assert job["input_sha256"].get(entry["source"]) == entry["source_sha256"], "Search initial population differs"
                        grammar = inputs[task]["grammar"]
                        assert job["input_sha256"].get(grammar) == inputs[task]["grammar_sha256"]
                        if variant != "asgp":
                            assert job["input_sha256"].get(entry["prepared"]) == entry["prepared_sha256"]
                        data = evidence.artifact(job)
                        if secondary_scope:
                            secondary.validate(data, freeze["variants"][variant]["mode"], job["cases"])
                        else:
                            speed.validate(data, freeze["variants"][variant]["mode"], job["cases"])
                        record["search_s"] = data["evolve_wall_ms"] / 1000
                        record.update(report_costs.post_search_audit(data, variant))
                        for key, value in freeze["variants"][variant]["extra"].items():
                            assert job["extra"].get(key) == value
                        assert len(evidence.artifact(job, "history")["history"]) == 64
                        exported = evidence.artifact(job, "export")
                        if variant != "asgp":
                            assert len(exported["programs"]) == (population if repetition == 0 else 1)
                        if repetition == 0:
                            outcomes = audit_r0(evidence, audits[variant], job, data, freeze, inputs[task])
                            successes[(task, variant)] = outcomes
                            r0_references[(task, variant)] = (data["final_population"], selected_trajectory(evidence.artifact(job, "history"), variant),
                                                             exported["best_program"] if variant == "asgp" else exported["programs"][0])
                        else:
                            final, trajectory, best = r0_references[(task, variant)]
                            assert data["final_population"] == final and selected_trajectory(evidence.artifact(job, "history"), variant) == trajectory
                            assert (exported["best_program"] if variant == "asgp" else exported["programs"][0]) == best
                        record.update(qualified=True, search_s=data["evolve_wall_ms"] / 1000)
                        loaded[variant] = (job, data)
                    except (AssertionError, KeyError, ValueError, OSError) as error:
                        record.update(status="evidence_invalid", reason=repr(error))
                        evidence.issues.append(dict(task=task, variant=variant, repetition=repetition, issue=repr(error)))
                metadata[variant] = record
                audit_rows.append(record)
            a, b, c = (metadata[v] for v in ("baseline", "candidate", "asgp"))
            row = dict(task=task, population=population, repetition=repetition, seed=freeze["seed"],
                       statuses=[a["status"], b["status"]], asgp_status=c["status"], speedup=None,
                       asgp_over_candidate=None, asgp_over_baseline=None,
                       timing_speedup=(a["search_s"] / b["search_s"] if a["search_s"] and b["search_s"] else None),
                       final_descriptors_equal=False, champion_ast_fitness_equal=False, final_export_equal=False)
            if a["qualified"] and b["qualified"]:
                jobs = {v: loaded[v][0] for v in loaded}
                x, y = (loaded[v][1] for v in ("baseline", "candidate"))
                histories = [[(r["ast"], r["fitness"]) for r in evidence.artifact(jobs[v], "history")["history"]]
                             for v in ("baseline", "candidate")]
                row.update(speedup=a["search_s"] / b["search_s"],
                           final_descriptors_equal=x["final_population"] == y["final_population"],
                           champion_ast_fitness_equal=histories[0] == histories[1],
                           final_export_equal=jobs["baseline"]["export_sha256"] == jobs["candidate"]["export_sha256"])
                if c["qualified"]:
                    row.update(asgp_over_candidate=c["search_s"] / b["search_s"], asgp_over_baseline=c["search_s"] / a["search_s"])
            rows.append(row)
    return rows, audit_rows, [dict(task=task, variant=variant, **outcomes) for (task, variant), outcomes in successes.items()]
