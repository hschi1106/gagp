"""Independently read the isolated packed-frame development evidence after exit.

No searches, selection, retries, or changes to the sealed formal0 report.
"""
from pathlib import Path
import argparse
import csv
import fcntl
import json
import math
import os
import re
import time

import development_v2
import serial


ARMS = ("baseline", "selected_control", "ordinary", "packed_aos", "packed_soa")
TERMINAL = {"stopped", "development_complete_only", "development_confirmed_only",
            "not_run_prerequisite_failed", "not_run_incremental_gain_below2pct", "budget_not_run"}
NATIVE_TESTS = {"gagp_test_extended_payload_gpu", "gagp_test_fitness_accumulator_gpu",
                "gagp_test_gpu_phase_population", "gagp_test_fitness_cpu_gpu_parity",
                "gagp_test_fitness_contract", "gagp_test_bounded_region_gpu"}


def full_mean(ratios, tasks):
    if not tasks or set(ratios) != set(tasks) or len(ratios) != len(tasks):
        return None
    values = list(ratios.values())
    if any(value is None or not math.isfinite(value) or value <= 0 for value in values):
        return None
    return math.exp(sum(map(math.log, values)) / len(values))


def native_tests_passed(log):
    passed = re.findall(r"^\s*\d+/6\s+Test\s+#\s*\d+:\s+(\S+)\s+\.{2,}\s+Passed\b", log, re.MULTILINE)
    return (len(passed) == 6 and set(passed) == NATIVE_TESTS and
            "100% tests passed, 0 tests failed out of 6" in log)


def verify_identity(stage, binary_path, expected_binary_hash):
    if stage["binary_sha256"] != expected_binary_hash or serial.sha(binary_path) != expected_binary_hash:
        raise ValueError("Wrong binary for development arm")


def verify_inputs(job, spec, extra):
    expected = {str(spec[k]): serial.sha(spec[k]) for k in ("source", "prepared", "grammar")}
    if expected != job["input_sha256"]:
        raise ValueError("Wrong fixed development cases/initials/grammar")
    if any(job["extra"].get(k) != v for k, v in extra.items()):
        raise ValueError("Wrong global variant recipe")


def read_search(stage, job, expected, cases):
    if stage["settings"] != expected:
        raise ValueError("Development base settings differ")
    if job["status"] != "ok":
        return None
    for field in ("output", "export", "history"):
        if serial.sha(job[field]) != job[field + "_sha256"]:
            raise ValueError("Changed " + field)
    for path, digest in job["input_sha256"].items():
        if serial.sha(path) != digest:
            raise ValueError("Changed development input")
    document = serial.read(job["output"])
    development_v2.validate(document, cases)
    programs = serial.read(job["export"])["programs"]
    history = serial.read(job["history"])["history"]
    if len(programs) != 1024 or len(history) != 64:
        raise ValueError("Incomplete native AST observations")
    seconds = document["evolve_wall_ms"] / 1000
    if seconds != job["search_s"]:
        raise ValueError("Search timing disagrees with raw output")
    return dict(document=document, programs=programs, history=history, seconds=seconds)


def run(prior_name, confirm_name, out, kind="packed"):
    out = Path(out)
    if out.exists():
        raise ValueError("Preserve previous report; use a new output path")
    protocol = serial.read(serial.BASE / "protocol.json")
    if kind not in ("packed", "groups"):
        raise ValueError("Unknown development layout")
    arms = ARMS if kind == "packed" else ("baseline", "selected_control", "ordinary", "current_groups")
    features = ("ordinary", "packed_aos", "packed_soa") if kind == "packed" else ("ordinary", "current_groups")
    controller = serial.read(serial.RAW / (prior_name + "-controller.json"))
    confirmation = serial.read(serial.RAW / (confirm_name + "-controller.json"))
    for state in (controller, confirmation):
        if state["status"] not in TERMINAL or Path(f"/proc/{state['pid']}").exists():
            raise ValueError("Existing controller is not terminal/exited; no competing analysis")
    if time.time() >= serial.DEADLINE:
        raise ValueError("Original hard deadline reached")
    with serial.GPU_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        source = serial.read(serial.RAW / prior_name / "source-manifest.json")
        for relative, digest in source["original_source_sha256"].items():
            if serial.sha(serial.ROOT / relative) != digest:
                raise ValueError("Official CPP changed")
        for relative, digest in source["source_sha256"].items():
            if serial.sha(Path(source["root"]) / relative) != digest:
                raise ValueError("Isolated CPP changed")
        stages = {}
        selected = serial.read(serial.RAW / "final-selection0.json")
        for variant in arms:
            path = serial.RAW / (prior_name + "-" + variant) / "manifest.json"
            stages[variant] = serial.read(path) if path.exists() else None
            if stages[variant]:
                expected = protocol["baseline_binary_sha256"] if variant == "baseline" else \
                    selected["binary_sha256"] if variant == "selected_control" else controller["binary_sha256"]
                verify_identity(stages[variant], path.parent / "bench", expected)
        rows, tasks, issues = [], [], []
        native = controller.get("steps", {}).get("native", {})
        native_log = Path(native["log"]).read_text() if native.get("log") else ""
        native_pass = (native.get("status") == "ok" and native.get("returncode") == 0 and
                       native_tests_passed(native_log))
        build = controller.get("steps", {}).get("build", {})
        for task in protocol["tasks"]:
            observations = {}
            for variant in arms:
                stage = stages[variant]
                job = stage["jobs"].get(f"{task}.s0.g64") if stage else None
                row = dict(task=task, variant=variant, population=1024, generations=64, seed=0,
                           status=job["status"] if job else "not_scheduled_after_prerequisite",
                           search_s=None, qualified=False, validation_error=None)
                try:
                    if job:
                        verify_inputs(job, protocol["development_inputs"][task], controller["arms"][variant])
                    observation = read_search(stage, job, protocol["baseline_settings"],
                                              protocol["development_inputs"][task]["train_count"]) if job else None
                    if observation:
                        row.update(search_s=observation["seconds"], qualified=native_pass)
                        observations[variant] = observation
                except (ValueError, AssertionError, KeyError, OSError) as error:
                    row["validation_error"] = repr(error)
                    issues.append(dict(task=task, variant=variant, error=repr(error)))
                rows.append(row)
            anchor = observations.get("baseline")
            equal = bool(anchor) and all(
                observation["programs"] == anchor["programs"] and
                observation["document"]["final_population"] == anchor["document"]["final_population"] and
                [(g["ast"], g["fitness"]) for g in observation["history"]] ==
                [(g["ast"], g["fitness"]) for g in anchor["history"]]
                for observation in observations.values())
            if not equal:
                for row in rows[-len(arms):]:
                    row["qualified"] = False
            complete = native_pass and equal and len(observations) == len(arms)
            current = observations.get("selected_control")
            item = dict(task=task, complete=complete)
            for variant in features:
                observation = observations.get(variant)
                item[variant] = current["seconds"] / observation["seconds"] if complete else None
                item[variant + "_vs_baseline"] = anchor["seconds"] / observation["seconds"] if complete else None
            tasks.append(item)
        means = {variant: full_mean({t["task"]: t[variant] for t in tasks}, protocol["tasks"])
                 for variant in features}
        baseline_means = {variant: full_mean({t["task"]: t[variant + "_vs_baseline"] for t in tasks}, protocol["tasks"])
                          for variant in features}
        out.mkdir()
        for name, data in (("runs.csv", rows), ("tasks.csv", tasks)):
            with (out / name).open("x", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(data[0]), lineterminator="\n")
                writer.writeheader(); writer.writerows(data)
        result = dict(scope="OLD TRAINING development only; not independent quality or formal ASGP result",
                      kind=kind, expected_searches=len(arms)*30, actual_searches=sum(r["status"] != "not_scheduled_after_prerequisite" for r in rows),
                      successful_searches=sum(r["status"] == "ok" for r in rows),
                      qualified_cells=sum(t["complete"] for t in tasks), native_regression_pass=native_pass,
                      expected_native_tests=sorted(NATIVE_TESTS), native_step=native, build_step=build,
                      controller_error=controller.get("error"),
                      controller_status=controller["status"], confirmation_status=confirmation["status"],
                      incremental_means_over_fresh_selected_control=means, means_over_fresh_beb_baseline=baseline_means, issues=issues,
                      formal0_old_over_new=1.0792574094191294, formal0_target_reached=False,
                      source_manifest_sha256=serial.sha(serial.RAW / prior_name / "source-manifest.json"),
                      report_source_sha256=serial.sha(Path(__file__)), finished_epoch=time.time())
        serial.save(out / "results.json", result)
        title = "Packed-frame" if kind == "packed" else "同代完全相同個體分組"
        lines = [f"# {title}追加開發結果", "",
                 "本表使用舊training，P1024／G64／seed0，每格一次。原正式結果仍為1.079×，未達1.3×。", "",
                 f"預訂{result['expected_searches']}次搜尋，實際建立{result['actual_searches']}次；程序OK{result['successful_searches']}次；{len(arms)}臂完整核對{result['qualified_cells']}/30題。",
                 f"隔離回歸：{'通過' if native_pass else '未通過／未完成'}。控制器狀態：{controller['status']}；條件式確認：{confirmation['status']}。", "",
                 "倍率＝本次新測selected-control搜尋時間÷本次新實作搜尋時間。完整30題才列幾何平均，失敗與未執行不刪除。", ""]
        if controller.get("error"):
            lines += [f"停止原因：`{controller['error']}`。建置狀態：`{build.get('status', 'not_run')}`；native狀態：`{native.get('status', 'not_run')}`。",
                      "建置逾時不提供語意、演化品質或加速證據；後續未執行的格子保留於CSV。", ""]
        for variant, value in means.items():
            lines.append(f"- {variant}：{value:.6f}×" if value is not None else f"- {variant}：未取得完整合格對照。")
        lines += ["", "相對這次新測beb5585d原基準（仍為舊training開發對照）：", ""]
        for variant, value in baseline_means.items():
            lines.append(f"- {variant}：{value:.6f}×" if value is not None else f"- {variant}：未取得完整合格對照。")
        lines += ["", "[逐次紀錄](runs.csv) · [逐題對照](tasks.csv) · [彙總](results.json)", "",
                  "這份開發表沒有新增heldout、ASGP分母或5pp非劣性證據，也不替換正式報告。數據來自未profile的搜尋wall；cold process與判題另計。"]
        (out / "report.md").write_text("\n".join(lines) + "\n")
        return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prior", default="packed-integration15")
    parser.add_argument("--confirm", default="packed-confirm15")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--kind", choices=("packed", "groups"), default="packed")
    args = parser.parse_args()
    print(json.dumps(run(args.prior, args.confirm, args.out, args.kind), indent=2))
