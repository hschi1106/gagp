"""P16384/G64 old-training capacity/parity pilot, separate from primary speed."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import time
import traceback

import serial


TASKS = ["is_sorted", "all_equal", "house_robber"]


def main(name, wait_controller):
    protocol = serial.read(serial.BASE / "protocol.json")
    lease = (serial.RAW / (name + "-controller.lock")).open("a")
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW / (name + "-controller.json")
    assert not record.exists()
    state = dict(pid=os.getpid(), status="waiting", started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 waiting_for=wait_controller, scope="3 fixed old TRAINING tasks; P16384/G64/C<=200; not all30 or quality proof",
                 harness_sha256={str(p): serial.sha(p) for p in [Path(__file__), serial.BASE / "serial.py", serial.BASE / "protocol.json"]})
    serial.save(record, state)
    try:
        while True:
            path = serial.RAW / (wait_controller + "-controller.json")
            prior = serial.read(path) if path.exists() else {}
            if prior.get("status") == "stopped":
                raise RuntimeError("Training prerequisite failed")
            if prior.get("status") == "requested_phase_complete" and not Path(f"/proc/{prior['pid']}").exists():
                break
            if time.time() >= protocol["development_cutoff_epoch"]:
                raise RuntimeError("Capacity pilot missed development cutoff")
            time.sleep(10)
        for path, digest in state["harness_sha256"].items():
            assert serial.sha(path) == digest
        old = serial.read(serial.ROOT / "logs/day24-20261017/manifest.json")
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            stages = {v: serial.Stage(name + "-" + v, protocol["baseline_binary"] if v == "baseline" else prior["binary"],
                                      settings=protocol["baseline_settings"]) for v in ("baseline", "candidate")}
        state["status"] = "old_training_capacity"
        serial.save(record, state)
        comparisons = []
        for index, task in enumerate(TASKS):
            inp = old["inputs"][task]
            prepared = inp["prefixes"]["16384"]
            source = stages["baseline"].out / (task + ".old-train.source.json")
            with serial.GPU_LOCK.open("a") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                assert serial.sha(inp["source"]) == inp["source_sha256"]
                assert serial.sha(prepared["prepared"]) == prepared["sha256"]
                assert serial.sha(inp["grammar"]) == inp["grammar_sha256"]
                training = serial.read(protocol["development_inputs"][task]["source"])
                document = serial.read(inp["source"])
                assert len(document["population"]) >= 16384
                document.update(cases=training["cases"], diagnostic_scope="old training capacity only; original P16384 source parents unchanged")
                serial.save(source, document)
            count = inp["split"]["train_count"]
            order = ("baseline", "candidate") if index % 2 == 0 else ("candidate", "baseline")
            for variant in order:
                stage = stages[variant]
                tag = task + ".p16384.s0.g64"
                output, export, history = [stage.out / (tag + suffix) for suffix in (".json", ".export.json", ".history.json")]
                extra = dict({} if variant == "baseline" else prior["candidate_extra"],
                             GAGP_SEARCH_GENERATIONS="64", GAGP_BM_SEED="0", GAGP_BM_CASE_COUNT=str(count),
                             GAGP_SEARCH_EXPORT=str(export), GAGP_SEARCH_HISTORY_EXPORT=str(history))
                job = stage.run(tag, ["measure", source, prepared["prepared"], inp["grammar"], 16384,
                                     "search_gpu_overlap", output], extra, timeout=3600, reserve=14400)
                with serial.GPU_LOCK.open("a") as lock:
                    fcntl.flock(lock, fcntl.LOCK_EX)
                    job.update(task=task, variant=variant, population=16384, generations=64, cases=count,
                               output=str(output), export=str(export), history=str(history))
                    if job["status"] == "ok":
                        try:
                            data = serial.read(output)
                            assert data["population"] == len(data["final_population"]) == 16384 and data["cases"] == count
                            assert len(data["generations"]) == 64
                            assert data["reproduction_profile"].startswith("native-gpu-phase-") and not data["reproduction_fallback_reason"]
                            assert data["verification_top_k"] == 0 and data["gpu_blocksize"] == data["cpu_audit_reduction_lanes"] == 512
                            assert data["cpu_audit_program_count"] == data["cpu_audit_wrong_program_count"] == 16
                            assert data["cpu_audit_mismatches"] == data["cpu_audit_wrong_mismatches"] == 0
                            assert max(r["nodes"] for r in data["final_population"]) <= 2048
                            assert all(r["cases"] == r["expected_program_cases"] == 16384 * count and r["unscored"] == 0
                                       and r["repro_kernel_ms"] > 0 for r in data["generations"])
                            assert len(serial.read(export)["programs"]) == 16384
                            job["search_s"] = data["evolve_wall_ms"] / 1000
                            for field, path in [("output", output), ("export", export), ("history", history)]:
                                job[field + "_sha256"] = serial.sha(path)
                        except Exception as error:
                            job.update(status="invalid_output", error=repr(error))
                    stage.persist()
            with serial.GPU_LOCK.open("a") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                jobs = [stages[v].m["jobs"][task + ".p16384.s0.g64"] for v in ("baseline", "candidate")]
                row = dict(task=task, statuses=[j["status"] for j in jobs], population=16384, generations=64)
                if all(j["status"] == "ok" for j in jobs):
                    docs = [serial.read(j["output"]) for j in jobs]
                    histories = [[(r["ast"], r["fitness"]) for r in serial.read(j["history"])["history"]] for j in jobs]
                    row.update(speedup=jobs[0]["search_s"] / jobs[1]["search_s"],
                               full_final_ast_equal=jobs[0]["export_sha256"] == jobs[1]["export_sha256"],
                               final_descriptors_equal=docs[0]["final_population"] == docs[1]["final_population"],
                               champions_equal=histories[0] == histories[1])
                comparisons.append(row)
                serial.save(serial.RAW / (name + "-comparisons.json"), comparisons)
        for stage in stages.values():
            stage.m.update(status="requested_phase_complete", finished_epoch=time.time())
            stage.persist()
        state.update(status="requested_phase_complete", finished_epoch=time.time(), comparisons=comparisons,
                     caveat="Only three capacity/parity observations; all30 secondary results and independent quality remain separate")
    except BaseException as error:
        state.update(status="stopped", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--wait-controller", required=True)
    args = parser.parse_args()
    main(args.name, args.wait_controller)
