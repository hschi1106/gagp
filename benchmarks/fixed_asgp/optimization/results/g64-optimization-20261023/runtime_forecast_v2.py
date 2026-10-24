"""Use full30 measured large TRAINING probes without multiplying startup cost."""
import math

import runtime_forecast
import serial

choose_scale = runtime_forecast.choose_scale


def large_probe_rows(probe, tasks):
    assert probe["status"] == "requested_phase_complete" and probe["full_cost_observed"] is True
    assert probe["diagnostic_only"] is True and probe["new_heldout"] is False
    rows = probe["observations"]
    index = {r["task"]: r for r in rows}
    assert len(rows) == len(index) == len(tasks) and set(index) == set(tasks)
    for row in rows:
        assert row["status"] == "measured"
        assert row["probe_count"] >= max(row["original_train_count"], row["forecast_test_count"]) > 0
        assert set(row["costs"]) == {"gagp_common", "gagp_native", "asgp_common", "asgp_native"}
        for kind, cost in row["costs"].items():
            assert cost["status"] == "ok" and math.isfinite(cost["process_s"]) and cost["process_s"] > 0
            if kind.endswith("native"):
                assert cost["native_fidelity_pass"] is True
    return index


def measured_costs(development_name, confirmation_name):
    costs, assumptions = runtime_forecast.measured_costs(development_name, confirmation_name)
    protocol = serial.read(serial.BASE / "protocol.json")
    tasks = protocol["tasks"]
    probe_path = serial.RAW / "judge-cost-large0-controller.json"
    probe = large_probe_rows(serial.read(probe_path), tasks)
    small = {r["task"]: r for r in serial.read(serial.RAW / "quality-cost0-controller.json")["observations"]}
    asgp_native = runtime_forecast.complete_jobs(serial.RAW / "asgp-calibration0-native/manifest.json", tasks, native=True)
    extra_native = []
    for variant in ("baseline", "candidate"):
        extra_native.extend(serial.read(serial.RAW / (confirmation_name + "-" + variant + "-native/manifest.json"))["jobs"].values())
    gagp = asgp = 0.0
    for task in tasks:
        train, large = small[task], probe[task]
        assert train["train_count"] == large["original_train_count"] == protocol["development_inputs"][task]["train_count"]
        assert train["forecast_test_count"] == large["forecast_test_count"]
        old_native = train["costs"]["gagp_native"]["process_s"]
        native = max([old_native] + [j["process_s"] for j in extra_native if j["task"] == task])
        # Preserve the slowest observed old-seed genotype effect. Only its
        # excess over the calibrated genotype scales with case count; the
        # complete measured large call already includes process/JSON startup.
        extra_test = max(0.0, native - old_native) * large["probe_count"] / train["train_count"]
        gagp += (train["costs"]["gagp_common"]["process_s"] + native +
                 large["costs"]["gagp_common"]["process_s"] + large["costs"]["gagp_native"]["process_s"] + extra_test)
        asgp += (train["costs"]["asgp_common"]["process_s"] + asgp_native[task]["process_s"] +
                 large["costs"]["asgp_common"]["process_s"] + large["costs"]["asgp_native"]["process_s"])
    # Duplicate probe inputs can help reuse. Add25% locally as well as the
    # unchanged global1.35 guard; neither is a completion guarantee.
    gagp *= 1.25
    asgp *= 1.25
    full = runtime_forecast.complete_jobs(serial.RAW / "full-audit-cost0/manifest.json", tasks, native=True)
    full_s = sum(j["process_s"] for j in full.values())
    costs.update(quality_judge_seed_s=2 * gagp, speed_audit_s=2 * full_s + 2 * gagp + asgp,
                 secondary_audit_s=32 * full_s + 2 * (2 * gagp + asgp))
    assumptions.update(model="full30 empirical large TRAINING judge calls v2",
                       large_training_probe=str(probe_path), large_training_probe_sha256=serial.sha(probe_path),
                       judge_local_safety=1.25, global_safety=1.35,
                       assumptions=[a for a in assumptions["assumptions"] if not a.startswith("Test cost scales")]+[
                           "Large judge probes repeat OLD TRAINING to at least test-count size; no heldout labels",
                           "Measured full-call cost replaces multiplying fixed startup by case ratio",
                           "Slowest old-seed native excess scales with cases, plus25% duplicate-input uncertainty",
                           "Fresh input/genotype runtime remains uncertain; original cutoff and all missing rows remain explicit"])
    return costs, assumptions
