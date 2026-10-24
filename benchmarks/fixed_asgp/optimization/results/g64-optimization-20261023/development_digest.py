"""Summarize closed development metadata without reading fresh formal results."""
import csv
import hashlib
import json
import math
from pathlib import Path

import serial


def main():
    sources, output = {}, []

    def read(name):
        path = serial.RAW / name
        data = path.read_bytes()
        sources[str(path)] = hashlib.sha256(data).hexdigest()
        return json.loads(data)

    descriptions = {
        "ordinary": "固定128 threads的view設定",
        "control32": "加上private launch bound32的控制組",
        "hybrid": "案例密集tiles＋hybrid view block（採用）",
        "specialized_parent": "private layout編譯特化",
        "combined": "hybrid加private layout編譯特化",
        "soa": "private cells改SoA排列",
        "short": "精確短list edit-distance路徑",
        "soa_short": "SoA加精確短list評分",
        "soa_short_dense": "SoA／短list加全面dense tiles",
        "bank": "小型register phase stack",
        "bank_narrow": "register stack加128-thread編譯bound",
        "bank_simple": "register stack加simple phase executor",
        "bank_no_measure": "register stack並關閉private measure cache",
    }
    groups = [
        ("private7", "sweep", 30, "sweep_geometric_means", "private7-sweep-all-comparisons.json", "control32"),
        ("layout8", "coarse", 8, "coarse_geometric_means", "layout8-coarse-all-comparisons.json", "ordinary"),
        ("bank9", "coarse", 8, "coarse_geometric_means", "bank9-coarse-all-comparisons.json", "ordinary"),
    ]
    for study, phase, count, field, comparison_file, control in groups:
        controller = read(study + "-controller.json")
        comparisons = read(comparison_file)
        assert controller["status"] == "requested_phase_complete"
        arms = controller[field]
        assert len(comparisons) == count * len(arms)
        for arm, recorded in arms.items():
            rows = [r for r in comparisons if r["variant"] == arm]
            assert len(rows) == count and len({r["task"] for r in rows}) == count
            assert all(all(r[key] is True for key in
                           ("final_descriptors_equal", "full_final_ast_equal", "champions_equal")) for r in rows)
            observed = math.exp(math.fsum(math.log(r["speedup"]) for r in rows) / count)
            assert math.isclose(observed, recorded, rel_tol=1e-12)
            manifests = {}
            for variant in ("baseline", arm):
                manifest = read(f"{study}-{phase}-{variant}/manifest.json")
                jobs = list(manifest["jobs"].values())
                assert len(jobs) == count and all(j["status"] == "ok" and j["population"] == 1024
                                                and j["generations"] == 64 and j["seed"] == 0 for j in jobs)
                manifests[variant] = {j["task"]: j for j in jobs}
            for row in rows:
                task = row["task"]
                ratio = manifests["baseline"][task]["search_s"] / manifests[arm][task]["search_s"]
                assert math.isclose(ratio, row["speedup"], rel_tol=1e-12)
            description = "已選hybrid設定（本階段控制組）" if arm == "ordinary" and study != "private7" else descriptions[arm]
            output.append(dict(study=study, arm=arm, description=description, population=1024,
                               generations=64, task_count=count, seed_count=1,
                               vs_round_initial_gagp=observed, same_stage_control=control,
                               vs_same_stage_control=observed / arms[control],
                               all_recorded_final_ast_scores_champions_equal=True,
                               scope="old_training_all30" if count == 30 else "old_training_screen8_only",
                               selection="selected" if study == "private7" and arm == "hybrid" else "control_or_not_selected"))
    confirm = read("private7-confirm-controller.json")
    comparisons = read("private7-confirm-comparisons.json")
    assert confirm["status"] == "requested_phase_complete" and len(comparisons) == 90
    assert {(r["task"], r["seed"]) for r in comparisons} == {(t, s) for t in serial.PAPER_TASKS for s in (0, 1, 2)}
    assert all(all(r[k] is True for k in ("final_descriptors_equal", "full_final_ast_equal", "champions_equal"))
               for r in comparisons)
    gm = math.exp(math.fsum(math.log(r["speedup"]) for r in comparisons) / len(comparisons))
    assert math.isclose(gm, confirm["geometric_mean"], rel_tol=1e-12)
    fields = list(output[0])
    with (serial.BASE / "development_attempts.csv").open("x", newline="") as file:
        writer = csv.DictWriter(file, fields)
        writer.writeheader()
        writer.writerows(output)
    evidence = dict(source_sha256=sources, generator_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    rows=output, confirmation_geometric_mean=gm,
                    confirmation_pairs=90, confirmation_seeds=[0, 1, 2],
                    per_seed_geometric_means=confirm["per_seed_geometric_means"],
                    scope="Closed old-TRAINING development metadata and timing manifests only; no fresh formal result read",
                    draft_label_correction="Initial draft mislabeled layout8/bank9 ordinary controls as fixed128; "
                         "prior bytes preserved in logs/development-digest-revision0, timing values unchanged",
                    caveat="Stored equality flags and final AST coverage are not CPU recomputation of every generation; "
                           "seed0 confirmation retains prior selected samples, not an additional repetition")
    with (serial.BASE / "development_attempts_evidence.json").open("x") as file:
        json.dump(evidence, file, ensure_ascii=False, indent=2)
        file.write("\n")
    lines = ["# G64開發期嘗試", "", "這是舊training上的選擇依據，不是正式新資料結果。所有列為P1024／G64，倍率皆為搜尋wall。", "",
             f"最後採用設定在完整30題、3個舊seeds的90組配對確認為 **{gm:.4f}×**，未達1.3×。seed0沿用先前的選定樣本，不能當作額外重複。", "",
             "| 範圍 | 嘗試 | 相對本輪原GAGP | 相對同階段控制組 |", "|---|---|---:|---:|"]
    for row in output:
        control = "—" if row["arm"] == row["same_stage_control"] else f"{row['vs_same_stage_control']:.4f}×"
        lines.append(f"|{row['task_count']}題／1seed|{row['description']}|{row['vs_round_initial_gagp']:.4f}×|{control}|")
    lines += ["", "30題的hybrid設定保留；編譯特化沒有增加收益。SoA、短list與register stack僅在8題篩選，未取得額外2%收益，沒有進入完整30題確認，也不能把其中約1.17×外推成整體結果。", "",
              "同階段控制組：private7使用control32；layout8與bank9使用ordinary（當時已選的hybrid設定）。每一階段都重新跑原始GAGP對照，不跨階段混用時間。", "",
              "表格核對了封閉task集合、成功狀態、P/G/seed、manifest搜尋時間比及保存的最終AST／分數描述／冠軍相等旗標；這不是重跑測試，也不代表每代全族群都做過CPU核對。來源SHA與精確數字見[證據](development_attempts_evidence.json)及[CSV](development_attempts.csv)。", "",
              "編譯失敗、第一版漏case失敗及未執行記錄仍保留於原始logs與RECOVERY歷史；本表只整理最後一次完整選擇與後續被拒絕的設定篩選，不是全部嘗試清單。"]
    with (serial.BASE / "development_attempts.md").open("x") as file:
        file.write("\n".join(lines) + "\n")
    print(json.dumps(dict(rows=len(output), development_confirmation=gm, fresh_formal_read=False)))


if __name__ == "__main__":
    main()
