"""Freeze a confirmed global TRAINING candidate and full grid before fresh data."""
from argparse import ArgumentParser
import fcntl
import math
import os
from pathlib import Path
import subprocess
import time
import traceback

import runtime_forecast_v2 as runtime_forecast
import serial
import source_provenance_v2 as source_provenance


RUNTIME_FILES = (
    "freeze_and_run.py", "runtime_forecast.py", "runtime_forecast_v2.py", "source_provenance_v2.py", "serial.py", "protocol.json",
    "quality_data.py", "prepare_inputs.py", "prepare_inputs_sharded.py", "quality.py", "quality_timed.py",
    "quality_sharded_timed.py", "champion_audit.py", "common_audit.py", "asgp_audit.py",
    "full_population_audit_timed.py", "budgeted_audits.py", "speed.py", "speed_audits_timed.py",
    "secondary.py", "development_v2.py", "study_statistics.py", "quality-scale-policy.json")


def confirmed_candidate(development, confirmation):
    assert development["status"] == confirmation["status"] == "requested_phase_complete"
    assert development["task_count"] == 30 and development["performance_admitted"] is True
    assert confirmation["comparisons"] == 90
    assert math.isfinite(confirmation["geometric_mean"]) and confirmation["geometric_mean"] >= 1.02
    assert confirmation["binary_sha256"] == development["binary_sha256"]
    assert confirmation["candidate_extra"] == development["candidate_extra"]
    return dict(binary=development["binary"], binary_sha256=development["binary_sha256"], extra=development["candidate_extra"])


def child_script(state, record, phase, script, arguments):
    command = ["python3", "-u", str(serial.BASE / script), *map(str, arguments)]
    if time.time() >= serial.DEADLINE - 14400:
        state.setdefault("phases", {})[phase] = dict(status="budget_not_run", inputs_not_read=True, command=command)
        serial.save(record, state)
        return
    log_path = serial.RAW / (state["name"] + "." + phase + ".log")
    with log_path.open("x") as log:
        child = subprocess.Popen(command, cwd=serial.ROOT, stdout=log, stderr=subprocess.STDOUT,
                                 env=dict(os.environ, PYTHONPATH=str(serial.ROOT / "tools")), start_new_session=True)
        if phase == "prepare_fresh_inputs":
            state.update(new_data_started=True, first_fresh_dispatch_epoch=time.time())
        state.update(status=phase, current_child_pid=child.pid, current_command=command)
        state.setdefault("phases", {})[phase] = dict(status="running", pid=child.pid, log=str(log_path), started_epoch=time.time())
        serial.save(record, state)
        result = child.wait()  # Each frozen Stage enforces the original timed cutoff.
    state["phases"][phase].update(status="exited", returncode=result, finished_epoch=time.time())
    serial.save(record, state)
    assert result == 0, f"{phase} stopped; inspect original records before explicit recovery"


def main(name, development_name, confirmation_name, checks_name):
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists(), "No implicit reruns or overwrite"
    state = dict(name=name, pid=os.getpid(), status="checking_training_evidence", started_epoch=time.time(),
                 deadline_epoch=serial.DEADLINE, development=development_name, confirmation=confirmation_name,
                 checks=checks_name, new_data_started=False)
    serial.save(record, state)
    try:
        protocol = serial.read(serial.BASE / "protocol.json")
        policy = serial.read(serial.BASE / "quality-scale-policy.json")
        assert policy["protocol_sha256"] == serial.sha(serial.BASE / "protocol.json")
        assert policy["allowed_seed_counts"] == [80, 40, 20, 10]
        assert protocol["started_epoch"] == serial.START and protocol["deadline_epoch"] == serial.DEADLINE
        assert tuple(protocol["primary"][k] for k in ("population", "generations", "repetitions")) == (1024, 64, 3)
        development = serial.read(serial.RAW / (development_name + "-controller.json"))
        confirmation = serial.read(serial.RAW / (confirmation_name + "-controller.json"))
        candidate = confirmed_candidate(development, confirmation)
        assert confirmation["prior"] == development_name
        assert all(not Path(f"/proc/{r['pid']}").exists() for r in (development, confirmation))
        rows = serial.read(serial.RAW / (confirmation_name + "-comparisons.json"))
        assert len(rows) == 90 and {(r["task"], r["seed"]) for r in rows} == {(t, s) for t in protocol["tasks"] for s in (0, 1, 2)}
        assert all(r[k] for r in rows for k in ("full_final_ast_equal", "final_descriptors_equal", "champions_equal"))
        quality_path = serial.RAW / (name + "-quality-freeze.json")
        speed_path = serial.RAW / (name + "-speed-freeze.json")
        secondary_path = serial.RAW / (name + "-secondary-freeze.json")
        assert not any(p.exists() for p in (quality_path, speed_path, secondary_path))
        assert not list(serial.RAW.glob("*-quality-freeze.json")), "A fresh cohort was already frozen; inspect, never replace it"
        checks_path = serial.RAW / checks_name / "manifest.json"
        checks = serial.read(checks_path)
        assert checks["status"] == "ok" and not Path(f"/proc/{checks['pid']}").exists()
        harness = [serial.BASE / p for p in RUNTIME_FILES]
        harness += [serial.ROOT / "benchmarks/fixed_asgp/optimization/results/day24-20261017/quality_data.py"]
        harness += sorted((serial.ROOT / "tools/gagp_tools").rglob("*.py"))
        for p in harness:
            if p.suffix == ".py" and p.parent == serial.BASE:
                assert checks["source_sha256"].get(str(p)) == serial.sha(p), "Runtime helper changed or was added after guards"
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert time.time() < protocol["development_cutoff_epoch"], "Freeze missed the original development cutoff"
            for spec in protocol["development_inputs"].values():
                assert all(serial.sha(path) == digest for path, digest in spec["sha256"].items()), "Original development input changed"
            provenance = source_provenance.collect(confirmation["source_build"])
            assert provenance["selected_binary_sha256"] == candidate["binary_sha256"]
            assert serial.sha(protocol["baseline_binary"]) == protocol["baseline_binary_sha256"]
            costs, assumptions = runtime_forecast.measured_costs(development_name, confirmation_name)
            forecast = runtime_forecast.choose_scale(time.time(), costs)
            forecast.update(assumptions=assumptions, protocol_sha256=serial.sha(serial.BASE / "protocol.json"))
            forecast_path = serial.RAW / (name + "-runtime-forecast.json")
            serial.save(forecast_path, forecast)
            if forecast["selected_seed_count"] is None:
                state.update(status="budget_insufficient", forecast=str(forecast_path), finished_epoch=time.time(),
                             reason="Full minimum grid does not fit pre-heldout rule; no new data or shorter-G substitute")
                serial.save(record, state)
                return
            provenance_path = serial.RAW / (name + "-source-provenance.json")
            serial.save(provenance_path, provenance)
            common = dict(started_epoch=serial.START, deadline_epoch=serial.DEADLINE, freeze_epoch=time.time(),
                          tasks=protocol["tasks"], generations=64, reserve_s=14400, data_seed=20261024,
                          settings=protocol["baseline_settings"], protocol_sha256=serial.sha(serial.BASE / "protocol.json"),
                          harness_sha256={str(p): serial.sha(p) for p in harness},
                          source_provenance=str(provenance_path), source_provenance_sha256=serial.sha(provenance_path),
                          forecast=str(forecast_path), forecast_sha256=serial.sha(forecast_path),
                          first_heldout_use="after immutable candidate/scale freeze")
            variants = {"baseline": dict(binary=protocol["baseline_binary"], binary_sha256=protocol["baseline_binary_sha256"], extra={}),
                        "candidate": candidate}
            quality_freeze = dict(common, format="gagp-g64-quality-freeze-v1", population=1024,
                                  seeds=list(range(35, 35 + forecast["selected_seed_count"])), train_limit=200, test_limit=1000,
                                  variants=variants, inputs={t: dict(grammar=protocol["development_inputs"][t]["grammar"]) for t in protocol["tasks"]})
            serial.save(quality_path, quality_freeze)
            speed_variants = {v: dict(s, mode="gpu_overlap") for v, s in variants.items()}
            # Fresh ASGP controls use the selected rebuilt binary, whose exact
            # external source and extension inputs were captured above.
            speed_variants["asgp"] = dict(binary=candidate["binary"], binary_sha256=candidate["binary_sha256"], extra={}, mode="asgp_1t")
            speed_freeze = dict(common, format="gagp-g64-speed-freeze-v1", population=1024, seed=35, repetitions=3,
                                variants=speed_variants, quality_freeze_sha256=serial.sha(quality_path))
            serial.save(speed_path, speed_freeze)
            serial.save(secondary_path, dict(common, format="gagp-g64-secondary-freeze-v1", population=16384, seed=35,
                                            repetitions=1, variants=speed_variants, quality_freeze_sha256=serial.sha(quality_path),
                                            cross_population_prefix_policy="Record equality; every P16384 method shares the same retained original source draw; never resample a different prefix"))
            state.update(status="frozen", freeze_epoch=common["freeze_epoch"], forecast=str(forecast_path),
                         selected_seed_count=forecast["selected_seed_count"], quality_freeze=str(quality_path),
                         speed_freeze=str(speed_path), secondary_freeze=str(secondary_path),
                         candidate_binary_sha256=candidate["binary_sha256"], candidate_extra=candidate["extra"],
                         development_geometric_mean=confirmation["geometric_mean"])
            serial.save(record, state)
        inputs_name = name + "-inputs"
        input_manifest = serial.RAW / (inputs_name + "-inputs.json")
        child_script(state, record, "prepare_fresh_inputs", "prepare_inputs_sharded.py", ["--freeze", quality_path, "--name", inputs_name])
        # Establish the complete primary result and its fidelity before the
        # larger secondary grid consumes the remaining fixed budget.
        child_script(state, record, "primary_speed", "speed.py", ["--freeze", speed_path, "--input-manifest", input_manifest, "--name", name + "-speed"])
        child_script(state, record, "primary_audits", "speed_audits_timed.py", ["--freeze", speed_path, "--input-manifest", input_manifest,
                                                                              "--speed-name", name + "-speed", "--name", name + "-audits"])
        child_script(state, record, "independent_quality", "quality_sharded_timed.py", ["--freeze", quality_path, "--input-manifest", input_manifest, "--name", name + "-quality"])
        child_script(state, record, "secondary_p16384", "secondary.py", ["--freeze", secondary_path, "--input-manifest", input_manifest, "--name", name + "-secondary"])
        state.update(status="validation_stages_finished", finished_epoch=time.time(),
                     caveat="Work stages ended; no success claim before independent source/input/coverage/native/fidelity/report audit")
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--development", required=True)
    parser.add_argument("--confirmation", required=True)
    parser.add_argument("--checks", required=True)
    args = parser.parse_args()
    main(args.name, args.development, args.confirmation, args.checks)
