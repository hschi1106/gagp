"""Build a readable closed-coverage report from this round's actual artifacts."""
from argparse import ArgumentParser
from collections import Counter
from datetime import datetime, timezone
import csv
import fcntl
import json
import math
import os
from pathlib import Path
import time
import traceback

import report_evidence
import report_costs
import report_metrics
import report_speed
import serial
import study_statistics as stats


def mean(values):
    values = list(values)
    return math.fsum(values) / len(values) if values else None


def gm(values):
    values = list(values)
    return stats.geometric_mean(values) if values else None


def execution_methods(extra):
    result = []
    if extra.get("GAGP_GPU_DENSE_CASE_TILES") == "tuple":
        result.append("TupleSummary 的私有 list 路徑把有案例的 lanes 集中成 tiles；200 cases／512 logical lanes 時由16個 tiles 改為7個，分數仍寫回原位置並按原順序加總。")
    elif extra.get("GAGP_GPU_DENSE_CASE_TILES") == "1":
        result.append("密集 case tiles 減少空 lanes；保留原始 logical score slots 與兩層加總順序，包含初次評分與溢位重跑。")
    if extra.get("GAGP_GPU_CASE_VIEW_BLOCK") == "hybrid":
        result.append("View kernel 依案例數及已編譯 phase 的加法特徵選 physical block；使用相同的全域規則，logical lanes 與評分案例保持不變。")
    elif extra.get("GAGP_GPU_CASE_VIEW_BLOCK") == "1":
        result.append("案例數少於 logical lanes 的四分之一時，縮小 view kernel 的 physical block，保留完整 logical reduction。")
    if extra.get("GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT") == "1":
        result.append("Host 已驗證的 private list 型別與記憶體配置轉為編譯期資訊，減少通用 VM 分支；未符合條件的程式使用原路徑。")
    if not result:
        result.append("採用固定的全域 GPU launch 設定；程式生成、選擇與突變規則保持一致。")
    return result


def analysis_sources():
    names = ("report.py", "report_evidence.py", "report_speed.py", "report_metrics.py",
             "report_costs.py", "study_statistics.py", "dashboard.html.in")
    return {str(serial.BASE / name): serial.sha(serial.BASE / name) for name in names}


def experiment_complete(primary, primary_asgp_complete, quality_complete, secondary, secondary_display, issues):
    """Complete-study coverage includes fresh ASGP controls at both populations."""
    return (primary["full_coverage"] and primary_asgp_complete and quality_complete
            and secondary["full_coverage"]
            and secondary_display["asgp_qualified_tasks"] == secondary_display["expected_tasks"]
            and not issues)


def closed_speed_quality(successes, jobs, tasks):
    known = {(r["task"], r["variant"]): r for r in successes}
    assert len(known) == len(successes)
    initial_round = {(r["task"], r["variant"]): r for r in jobs if r["repetition"] == 0}
    result = []
    for task in tasks:
        for variant in ("baseline", "candidate", "asgp"):
            key = task, variant
            job = initial_round[key]
            row = dict(task=task, variant=variant, seed=35, status=job["status"], search_status=job["search_status"],
                       fully_audited=key in known, initial_success=None, final_success=None, newly_solved=None)
            if key in known:
                row.update(known[key])
            result.append(row)
    return result


def csv_file(path, rows):
    rows = list(rows)
    keys = list(dict.fromkeys(k for row in rows for k in row))
    with Path(path).open("x", newline="") as output:
        writer = csv.DictWriter(output, keys)
        writer.writeheader()
        for row in rows:
            writer.writerow({k: json.dumps(v, ensure_ascii=False) if isinstance(v, (dict, list, tuple)) else v for k, v in row.items()})


def quality_summary(rows, pairs, tasks, seeds):
    task_rows = []
    for task in tasks:
        selected = [row for row in pairs if row["task"] == task]
        assert len(selected) == len(seeds)
        # An unknown pair contributes the worst discordance to uncertainty.
        # Never convert two unknown outcomes into evidence of agreement.
        b = [r["baseline_success"] if r["complete_pair"] else True for r in selected]
        c = [r["candidate_success"] if r["complete_pair"] else False for r in selected]
        interval = stats.paired_success(b, c)
        curves = {}
        counts = {}
        paired_seeds = {r["seed"] for r in selected if r["complete_pair"]}
        for variant in ("baseline", "candidate"):
            valid = [r for r in rows if r["task"] == task and r["variant"] == variant and r["complete"]]
            counts[variant] = dict(complete=len(valid), initial=sum(r["initial_success"] for r in valid),
                                   final=sum(r["final_success"] for r in valid), new=sum(r["newly_solved"] for r in valid))
            paired = [r for r in valid if r["seed"] in paired_seeds]
            curves[variant] = {split: [mean(r[split + "_accuracy_history"][i] for r in paired) for i in range(65)]
                               for split in ("train", "test")} if paired else {}
        task_rows.append(dict(task=task, quality_expected=len(seeds), quality_complete=sum(r["complete_pair"] for r in selected),
                              baseline_success=counts["baseline"]["final"], candidate_success=counts["candidate"]["final"],
                              success_difference_ci95=interval["difference_ci95"], uncertainty_unknown_pairs_as_losses=True,
                              counts=counts, curves=curves))
    clustered = stats.clustered_quality_bound(pairs, tasks, seeds)
    return task_rows, clustered


def frozen(path, reader):
    result = serial.read(path)
    assert result["started_epoch"] == serial.START and result["deadline_epoch"] == serial.DEADLINE
    assert result["generations"] == 64
    assert result["tasks"] == list(serial.PAPER_TASKS)
    for file, digest in result["harness_sha256"].items():
        reader.verify(file, digest)
    for variant in result["variants"].values():
        reader.verify(variant["binary"], variant["binary_sha256"])
    reader.verify(result["source_provenance"], result["source_provenance_sha256"])
    provenance = serial.read(result["source_provenance"])
    assert provenance["selected_binary_sha256"] == result["variants"]["candidate"]["binary_sha256"]
    reader.verify(provenance["build_manifest"], provenance["build_manifest_sha256"])
    for group in ("cpp_source_sha256", "asgp_source_sha256", "asgp_overlay_sha256", "build_input_sha256"):
        for file, digest in provenance[group].items():
            reader.verify(serial.ROOT / file if group == "cpp_source_sha256" else file, digest)
    result["_sha256"] = serial.sha(path)
    return result, provenance


def prepared_inputs(formal_name, freeze):
    aggregate = serial.RAW / (formal_name + "-inputs-inputs.json")
    if aggregate.exists():
        doc = serial.read(aggregate)
        assert doc["freeze_sha256"] == freeze["_sha256"]
        assert set(doc["inputs"]) == set(freeze["tasks"])
        return doc["inputs"]
    # An interrupted preparation may not have written its final aggregate.
    # Recover already journaled inputs; every remaining task stays missing.
    result = {}
    for task in freeze["tasks"]:
        path = serial.RAW / (formal_name + "-inputs-" + task) / "manifest.json"
        if path.exists():
            doc = serial.read(path)
            assert doc["freeze_sha256"] == freeze["_sha256"]
            result[task] = doc.get("inputs", {}).get(task, dict(status="missing", seeds={}))
        else:
            result[task] = dict(status="missing", seeds={})
    return result


def plots(out, tasks, summary):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    case_range = summary.get("training_case_range")
    case_label = f"Train cases/task: {case_range[0]}–{case_range[1]}" if case_range else "Train case coverage incomplete"
    with plt.rc_context({"font.size": 9, "axes.spines.top": False, "axes.spines.right": False,
                         "savefig.facecolor": "white", "figure.facecolor": "white"}):
        fig, ax = plt.subplots(figsize=(8.2, 8.6))
        qualified = [(i, row["speedup"]) for i, row in enumerate(tasks) if row["speedup"] is not None]
        for i, value in qualified:
            ax.plot([1, value], [i, i], color="0.3", linewidth=5, solid_capstyle="butt")
            interval = tasks[i].get("timing_ci95")
            if interval:
                ax.errorbar(value, i, xerr=[[max(0, value - interval[0])], [max(0, interval[1] - value)]],
                            fmt="o", color="black", markersize=2.5, elinewidth=.8, capsize=2)
        ax.set_yticks(range(len(tasks)), [r["task"] for r in tasks], fontsize=8)
        ax.invert_yaxis()
        ax.axvline(1, color="black", linewidth=.8)
        ax.axvline(1.3, color="0.4", linestyle="--", linewidth=.8, label="Target1.3x")
        ax.set_xscale("log")
        ax.set_xlabel("Previous GAGP / candidate search time (higher is faster)")
        ax.set_title("All30 tasks | P1024 | G64 |3 paired timing rounds\n" + case_label, fontsize=10)
        ax.text(0, -.085, "Whiskers:95% paired-round bootstrap interval; fixed tasks and initial seed",
                transform=ax.transAxes, fontsize=8)
        ax.legend(frameon=False)
        fig.tight_layout()
        for suffix in ("png", "pdf"):
            fig.savefig(out / ("speed." + suffix), dpi=180)
        plt.close(fig)
        fig, axes = plt.subplots(1, 2, figsize=(9, 3.4), sharey=True)
        for ax, split in zip(axes, ("train", "test")):
            for variant, style in (("baseline", "-"), ("candidate", "--")):
                arrays = [r["curves"][variant].get(split) for r in tasks if r["curves"][variant].get(split)]
                if arrays:
                    curve = [mean(a[i] for a in arrays) for i in range(65)]
                    ax.plot(range(65), curve, style, color="black" if variant == "candidate" else "0.5", label=variant)
            ax.set(xlabel="Generation", title=split.capitalize(), xlim=(0, 64), ylim=(0, 1.02))
            ax.legend(frameon=False)
        axes[0].set_ylabel("Exact output accuracy (completed audits)")
        fig.suptitle(f"P1024 | G64 | {summary['seed_count']} planned evolution seeds/task | {case_label}\n"
                     f"Audited pairs:{summary['quality_complete_pairs']}/{summary['quality_expected_pairs']} | task means weighted equally",
                     fontsize=10)
        fig.tight_layout()
        for suffix in ("png", "pdf"):
            fig.savefig(out / ("quality." + suffix), dpi=180)
        plt.close(fig)


def render_files(out, dataset):
    serial.save(out / "results.json", dataset)
    html = (serial.BASE / "dashboard.html.in").read_text().replace(
        "__STUDY_JSON__", json.dumps(dataset, ensure_ascii=False, allow_nan=False).replace("<", "\\u003c"))
    (out / "dashboard.html").write_text(html)
    s = dataset["summary"]
    number = lambda x: f"{x:.3f}×" if x is not None else "未取得全範圍有效結果"
    count = lambda key: str(s[key]) if s[key] is not None else "未知"
    audit_time = lambda key: f"{s[key]:.3f}秒" if s[key] is not None else "未知"
    counts = s.get("training_case_range")
    cases = f"Training每題{counts[0]}–{counts[1]} cases；test每題最多1000，有限輸入域較少。" if counts else "Fixture準備未完整，實際cases數見CSV。"
    lines = ["# GAGP G64：速度與品質", "", f"P=1024，G=64；30題，速度重複3次；GAGP獨立品質{ s['seed_count']}個新seeds。", "", cases, "",
             f"- 相對原最快GAGP：**{number(s['primary_speedup'])}**。有效速度覆蓋{s['primary_qualified_tasks']}/30題；目標1.3×{'已達到' if s['target_reached'] else '尚未證明達到'}。",
             f"- 相對本輪ASGP1T：**{number(s['asgp_speedup'])}**。分母為本輪相同P/G/cases的未profile搜尋時間。",
             f"- 獨立品質完整判題{s['quality_complete_pairs']}/{s['quality_expected_pairs']}對；最終train+test全對：{s['baseline_final_success']}→{s['candidate_final_success']}。",
             f"- 初代已全對：{s['baseline_initial_success']}→{s['candidate_initial_success']}；後續新解：{s['baseline_new_success']}→{s['candidate_new_success']}。", "",
             f"ASGP品質只判速度cohort的seed35：完成{s.get('asgp_quality_qualified_tasks',0)}/30題；可確認最終全對{s.get('asgp_final_success',0)}/30。這與GAGP的多seed品質表分開呈現。", "",
             s["secondary_text"], "", s["secondary_memory_text"], "",
             "倍率包含GPU評分與GPU繁殖。CPU16搜尋後抽查與離線原生CPU全族群／逐代冠軍判題的成本另列。", "",
             f"搜尋中CPU核對：有記錄{s['in_search_cpu_verification_known']}/{s['post_search_cpu_audit_expected']}次GPU搜尋；已知fitness修正{count('in_search_cpu_fitness_corrections')}次、wrong修正{count('in_search_cpu_wrong_corrections')}次、best變更{count('in_search_cpu_best_changes')}次，記錄耗時{audit_time('in_search_cpu_verification_s')}。此耗時已包含在搜尋wall time。", "",
             f"搜尋後CPU稽核區段：已記錄{s['post_search_cpu_audit_recorded_s']:.1f}秒（{s['post_search_cpu_audit_known']}/{s['post_search_cpu_audit_expected']}次GPU搜尋）。包括cases準備、全族群identity及top16編譯／CPU評分，不是單純CPU評分時間。", "",
             f"離線判題process wall：已記錄{s['offline_recorded_process_s']:.1f}秒，含失敗呼叫{s['offline_failed_recorded_process_s']:.1f}秒；另有{s['offline_unknown_started_costs']}筆已開始但成本未知。缺漏不當作零。這包含GPU／ASGP native及共同判題，不是純CPU時間。",
             f"其中通過證據核對的判題呼叫為{s['judge_process_s']:.1f}秒；搜尋後與離線成本不加入headline搜尋倍率，不含本次報告讀取。", "",
             "[互動結果](dashboard.html) · [逐題速度](speed.csv) · [P16384逐題對照](secondary_tasks.csv) · [品質明細](quality.csv) · [品質配對](quality_pairs.csv) · [速度cohort品質](speed_quality.csv) · [離線驗證成本](audit_costs.csv) · [原始彙總](results.json)", "",
             "## 逐題結果", "", "| Task | GAGP倍率 | ASGP／新GAGP | 品質全對：舊→新／預訂seeds |", "|---|---:|---:|---:|"]
    for row in dataset["tasks"]:
        lines.append(f"|{row['task']}|{number(row['speedup'])}|{number(row['asgp_over_candidate'])}|{row['baseline_success']}→{row['candidate_success']}/{row['quality_expected']}|")
    lines += ["", "## P16384逐題對照", "", "G=64，每題1輪；倍數只顯示通過既有原始證據核對的對照。未完整核對的列仍保留，完整失敗狀態見CSV。", "",
              "| Task | 舊GAGP／新GAGP | ASGP1T／新GAGP | 三方法對照 |", "|---|---:|---:|---|"]
    for row in dataset["secondary_tasks"]:
        lines.append(f"|{row['task']}|{number(row['speedup'])}|{number(row['asgp_over_candidate'])}|{'完整' if row['complete'] else '未完整'}|")
    lines += ["", "## 採用的GPU執行方式", ""] + ["- " + value for value in dataset.get("execution_methods", [])]
    lines += ["", "## 限制與驗證範圍", ""] + ["- " + value for value in dataset["limitations"]]
    (out / "report.md").write_text("\n".join(lines) + "\n")


def main(formal_name, out):
    out.mkdir()  # Never overwrite an earlier successful or failed analysis.
    record = out / "completion.json"
    state = dict(pid=os.getpid(), status="waiting_for_lock", started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 formal=formal_name, note="Analysis completion alone is not experimental success")
    serial.save(record, state)
    try:
        controller = serial.read(serial.RAW / (formal_name + "-controller.json"))
        assert not Path(f"/proc/{controller['pid']}").exists(), "Formal controller is still active"
        assert controller["status"] in ("validation_stages_finished", "stopped")
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert time.time() < serial.DEADLINE
            state["status"] = "reading_and_verifying"
            state["analysis_source_sha256"] = analysis_sources()
            serial.save(record, state)
            reader = report_evidence.Evidence()
            q, provenance = frozen(serial.RAW / (formal_name + "-quality-freeze.json"), reader)
            speed_freeze, _ = frozen(serial.RAW / (formal_name + "-speed-freeze.json"), reader)
            second, _ = frozen(serial.RAW / (formal_name + "-secondary-freeze.json"), reader)
            assert speed_freeze["quality_freeze_sha256"] == second["quality_freeze_sha256"] == q["_sha256"]
            inputs = prepared_inputs(formal_name, q)
            splits = report_evidence.validate_splits(inputs, q, reader)
            secondary_draws = report_evidence.secondary_inputs(formal_name + "-secondary", inputs, second, reader)
            quality_rows, quality_pairs = report_evidence.collect_quality(formal_name + "-quality", inputs, q, reader)
            task_rows, clustered = quality_summary(quality_rows, quality_pairs, q["tasks"], q["seeds"])
            speed_rows, speed_jobs, speed_success = report_speed.collect(formal_name + "-speed", formal_name + "-audits", speed_freeze, inputs, reader)
            second_rows, second_jobs, second_success = report_speed.collect(formal_name + "-secondary", "", second, inputs, reader)
            audit_cost_rows, audit_cost_sources = report_costs.collect_offline(formal_name, q, speed_freeze, second, reader)
            cost_summary = report_costs.summarize(dict(primary=speed_jobs, quality=quality_rows, secondary=second_jobs), audit_cost_rows)
            primary = stats.speed_summary(speed_rows, q["tasks"], [1024], 3)
            secondary_stats = stats.speed_summary(second_rows, q["tasks"], [16384], 1)
            secondary_tasks, secondary_display = report_metrics.secondary_display(second_rows, secondary_stats["cells"], q["tasks"])
            secondary_memory = report_metrics.card_memory_summary(second_jobs, 16384)
            for row, cell in zip(task_rows, primary["cells"]):
                assert row["task"] == cell["task"]
                rows = [r for r in speed_rows if r["task"] == row["task"]]
                asgp = [r["asgp_over_candidate"] for r in rows if r["asgp_over_candidate"] is not None]
                row.update(speedup=cell["speedup"] if cell["qualified"] else None,
                           asgp_over_candidate=gm(asgp) if len(asgp) == 3 else None,
                           timing_ci95=report_metrics.paired_round_interval([[r["speedup"] for r in rows]])["timing_ci95"] if cell["qualified"] else None)
            group, secondary_group = primary["groups"]["1024"], secondary_stats["groups"]["16384"]
            complete = sum(r["complete_pair"] for r in quality_pairs)
            all_quality = complete == len(quality_pairs) and all(r["trajectory_equal"] for r in quality_pairs)
            full_asgp = all(r["asgp_over_candidate"] is not None for r in speed_rows)
            timing = report_metrics.paired_round_interval([[r["speedup"] for r in speed_rows if r["task"] == task] for task in q["tasks"]]) if group["full_coverage"] else None
            s = dict(primary_speedup=group["observed_geometric_mean"] if group["full_coverage"] else None,
                     speed_ci95=timing["timing_ci95"] if timing else None,
                     asgp_speedup=gm(r["asgp_over_candidate"] for r in speed_rows) if full_asgp else None,
                     primary_qualified_tasks=group["qualified_cells"], repetitions=3, seed_count=len(q["seeds"]),
                     quality_complete_pairs=complete, quality_expected_pairs=len(quality_pairs),
                     target_reached=group["target_reached"] and all_quality,
                     all_observed_quality_trajectories_equal=all_quality,
                     judge_process_s=reader.judge_process_s,
                     native_calls=reader.native_calls, native_logical_program_observations=reader.native_observations,
                     native_executed_unique_programs=reader.native_executed,
                     asgp_quality_qualified_tasks=sum(row["variant"] == "asgp" for row in speed_success),
                     asgp_final_success=sum(row["variant"] == "asgp" and row["final_success"] for row in speed_success),
                     secondary_asgp_speedup=secondary_display["asgp_speedup"],
                     secondary_asgp_qualified_tasks=secondary_display["asgp_qualified_tasks"],
                     secondary_text=f"P16384/G64：GAGP配對完整核對{secondary_group['qualified_cells']}/30題；ASGP三方法有效對照{secondary_display['asgp_qualified_tasks']}/30題；每題一輪。" +
                         (f"相對舊GAGP幾何平均{secondary_group['observed_geometric_mean']:.3f}×。" if secondary_group["full_coverage"] else "未取得全30有效GAGP倍率。") +
                         (f"ASGP1T／新GAGP幾何平均{secondary_display['asgp_speedup']:.3f}×。" if secondary_display["asgp_speedup"] is not None else "未取得全30有效ASGP倍率；缺漏與失敗仍在CSV。"))
            s.update(cost_summary)
            peak = secondary_memory["peak_bytes"]
            peak_text = f"{peak / 1024**3:.2f}GiB" if peak is not None else "未知"
            s.update(secondary_memory=secondary_memory,
                     secondary_memory_text=f"P16384整卡VRAM：有有效取樣{secondary_memory['sampled_gpu_searches']}/{secondary_memory['expected_gpu_searches']}次GPU搜尋，觀測最大{peak_text}；缺漏保留未知。"
                         f"搜尋成功{secondary_memory['successful_gpu_searches']}/{secondary_memory['expected_gpu_searches']}次，通過指定CPU稽核{secondary_memory['qualified_gpu_searches']}/{secondary_memory['expected_gpu_searches']}次。"
                         "約200ms取樣，包含桌面與失敗呼叫，可能漏掉短暫峰值，並非精確最大用量或普遍容量保證。")
            for variant in ("baseline", "candidate"):
                valid = [r for r in quality_rows if r["variant"] == variant and r["complete"]]
                for name, field in (("final", "final_success"), ("initial", "initial_success"), ("new", "newly_solved")):
                    s[variant + "_" + name + "_success"] = sum(r[field] for r in valid)
            case_counts = [row["train_count"] for row in splits if row.get("input_disjoint")]
            s["training_case_range"] = [min(case_counts), max(case_counts)] if case_counts else None
            limitations = [
                "固定30題、P1024/G64；速度3輪共用同一新初代seed35。時間區間只描述這3輪，不代表新任務或新初代的性能分布。",
                "倍率採已固定的engine搜尋wall。兩個GAGP版本的main.cpp計時邊界相同；ASGP從parents複製／grammar建立前開始，含每代history編碼，GAGP使用已編譯grammar、JSON匯出與CPU16稽核在搜尋後。兩者涵蓋的準備／記錄工作不同；完整CLI process時間另列。",
                "品質資料seed20261024，演化seeds35起；每題train/test輸入互斥、跨seed相同。有限輸入域與歷史資料重疊在split metadata揭露。",
                "品質未知或失敗保留在預訂分母；非劣性界以seed為cluster，未知對視為退步。" + ("5pp界獲支持。" if clustered["supports_noninferiority"] else "目前不足以支持5pp非劣性。"),
                "核對範圍為每代冠軍、最終族群分數描述與指定完整族群稽核；不代表每一代所有個體都已CPU重算，也不宣稱普遍等價。fitness採原40bit ranking canonicalization。",
                "ASGP1T為本輪新分母，使用此repo的ASGP整合及list-combine extension；外部來源資料夾缺Git metadata，精確檔案hash保留，無論文重現宣稱。",
                "ASGP與GAGP的原生算子及grammar sites未全面對齊；ASGP速度倍率不代表兩者演化能力相等。",
                "P16384獨立列示；跨P初代前綴不保證相同。每個P16384方法共用同一保留draw，未重抽失敗或不准入個體。",
                "NVML每約200ms取樣整張卡，包含桌面占用，可能漏掉短暫峰值。搜尋後／離線CPU判題成本與cold process時間另列，不併入headline搜尋倍率。",
                "驗證成本只加總實際記錄：保留失敗呼叫耗時，未知／未執行不作零成本。cpu_audit_ms涵蓋cases準備、identity與top16編譯／CPU評分；離線process另含GPU工作，兩者都不代表純CPU評分時間。"]
            if reader.issues:
                limitations.append(f"原始證據核對發現{len(reader.issues)}筆問題；對應樣本保留為evidence_invalid，不能作完整成功宣稱。")
            dataset = dict(summary=s, tasks=task_rows, quality_cluster_bound=clustered, primary_speed=primary,
                           secondary_speed=secondary_stats, secondary_tasks=secondary_tasks,
                           speed_success=speed_success, secondary_success=second_success,
                           split_audits=splits, secondary_input_audits=secondary_draws, limitations=limitations, issues=reader.issues,
                           execution_methods=execution_methods(q["variants"]["candidate"]["extra"]),
                           source_provenance=provenance, frozen_quality=q,
                           analysis_source_sha256=state["analysis_source_sha256"],
                           audit_cost_sources=audit_cost_sources,
                           search_status_counts=dict(Counter(j["search_status"] for j in speed_jobs)),
                           verified_files=len(reader.hashes), generated_utc=datetime.now(timezone.utc).isoformat())
            csv_file(out / "speed.csv", speed_jobs)
            csv_file(out / "speed_pairs.csv", speed_rows)
            csv_file(out / "speed_quality.csv", closed_speed_quality(speed_success, speed_jobs, q["tasks"]))
            csv_file(out / "secondary.csv", second_jobs)
            csv_file(out / "secondary_pairs.csv", second_rows)
            csv_file(out / "secondary_tasks.csv", secondary_tasks)
            csv_file(out / "quality.csv", quality_rows)
            csv_file(out / "quality_pairs.csv", quality_pairs)
            csv_file(out / "audit_costs.csv", audit_cost_rows)
            serial.save(out / "audit_costs.json", dict(summary=cost_summary, sources=audit_cost_sources))
            csv_file(out / "tasks.csv", [{k: v for k, v in row.items() if k != "curves"} for row in task_rows])
            render_files(out, dataset)
            plots(out, task_rows, s)
            assert analysis_sources() == state["analysis_source_sha256"], "Report source/template changed during generation"
            state["output_sha256"] = {p.name: serial.sha(p) for p in sorted(out.iterdir())
                                      if p.is_file() and p != record}
            state.update(status="artifacts_ready_manual_review_pending", finished_epoch=time.time(),
                         target_reached=s["target_reached"],
                         full_experiment_coverage=experiment_complete(group, full_asgp, all_quality,
                             secondary_group, secondary_display, reader.issues),
                         within_original_deadline=time.time() <= serial.DEADLINE,
                         remaining="Inspect plots/UI, actual final regressions, affected docs/diff, localcommit and original HTML render")
    except BaseException as error:
        state.update(status="failed", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--formal", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    main(args.formal, args.out.resolve())
