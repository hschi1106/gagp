"""Verify closed old-training evidence; admission is separate from the1.3x goal."""
import freeze_and_run


def verify(development, confirmation, comparisons, native, tasks):
    candidate = freeze_and_run.confirmed_candidate(development, confirmation)
    assert len(tasks) == len(set(tasks)) == 30
    assert len(comparisons) == 90
    assert {(r["task"], r["seed"]) for r in comparisons} == {(t, s) for t in tasks for s in (0, 1, 2)}
    assert all(r[k] is True for r in comparisons for k in ("full_final_ast_equal", "final_descriptors_equal", "champions_equal"))
    assert set(native) == {"baseline", "candidate"}
    for variant in ("baseline", "candidate"):
        stage = native[variant]
        assert stage["status"] == "requested_phase_complete"
        jobs = list(stage["jobs"].values())
        assert len(jobs) == 60
        assert {(j["task"], j["seed"], j["split"]) for j in jobs} == {(t, s, "train") for t in tasks for s in (1, 2)}
        assert all(j["status"] == "ok" and j["native_fidelity_pass"] is True and not j["native_mismatches"] for j in jobs)
        assert all(j["native_program_observations"] == 65 for j in jobs)
    return dict(candidate=candidate, geometric_mean=confirmation["geometric_mean"],
                performance_target=1.3, development_target_met=confirmation["geometric_mean"] >= 1.3,
                trajectory_pairs=90, native_training_calls=120,
                scope="Three old-training seeds with matched trajectories; native65champions on seeds1/2, seed0 search CPU16 only; not fresh validation")
