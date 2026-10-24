"""Fail closed on actual capacity parity records before any fresh-data dispatch."""
TASKS = {"is_sorted", "all_equal", "house_robber"}
FIELDS = ("full_final_ast_equal", "final_descriptors_equal", "champions_equal")


def check_capacity(dispatch, pilot, manifests, candidate_sha256, baseline_sha256):
    """Check the closed pilot grid; requested completion alone is insufficient.

    Raw output hashes and CPU16 checks originate in the immutable capacity
    runner. Final reporting independently verifies raw outputs and larger
    native audits. This gate does not upgrade sampled CPU coverage to full
    population equivalence.
    """
    if dispatch.get("status") == "budget_not_run":
        assert dispatch.get("no_samples") is True
        assert pilot is None and not manifests, "A sampled pilot cannot become not-run"
        return {"status": "not_run_budget", "scope": "No new candidate P16384 capacity proof"}
    assert dispatch.get("status") == "requested_phase_complete"
    assert dispatch.get("returncode") == 0
    assert pilot and pilot.get("status") == "requested_phase_complete"
    rows = pilot["comparisons"]
    assert len(rows) == 3 and {r["task"] for r in rows} == TASKS
    assert all(r["population"] == 16384 and r["generations"] == 64 for r in rows)
    assert all(r["statuses"] == ["ok", "ok"] for r in rows), "Capacity failures remain failures"
    assert all(r.get(k) is True for r in rows for k in FIELDS), "Observed capacity parity mismatch"
    assert set(manifests) == {"baseline", "candidate"}
    for variant, expected_hash in (("baseline", baseline_sha256), ("candidate", candidate_sha256)):
        stage = manifests[variant]
        assert stage["status"] == "requested_phase_complete"
        assert stage["binary_sha256"] == expected_hash
        jobs = stage["jobs"]
        assert set(jobs) == {t + ".p16384.s0.g64" for t in TASKS}
        for task in TASKS:
            job = jobs[task + ".p16384.s0.g64"]
            assert job["status"] == "ok" and job["task"] == task and job["variant"] == variant
            assert (job["population"], job["generations"]) == (16384, 64)
            assert all(job.get(k + "_sha256") for k in ("output", "export", "history"))
    return {"status": "observed_parity_pass", "tasks": 3, "searches": 6, "cpu_programs_per_search": 16,
            "scope": "Final16384 AST/score descriptors and64 selected champions, sampled CPU16 only"}
