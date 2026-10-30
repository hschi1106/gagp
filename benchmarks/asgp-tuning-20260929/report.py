"""Generate the compact tuning report from measured CSVs."""
import csv
import html
import json
import statistics
from pathlib import Path

from run import HERE, OUT, SETTINGS, identity, read_rows


def median(rows, key):
    return statistics.median(float(r[key]) for r in rows if int(r["rep"]) >= 0)


def main():
    manifest = json.loads((HERE / "manifest.json").read_text())
    assert manifest["sha256"] == identity(), "Artifact identity changed"
    selected = tuple(manifest["selected"])
    baseline = next(r for r in csv.DictReader(
        (HERE.parent / "asgp-quick-20260929/summary.csv").open())
        if int(r["population"]) == 1024 and int(r["cases"]) == 1024)
    cpu = float(baseline["asgp_1"])
    measured = []
    all_rows = []
    for f, b in SETTINGS:
        rows = read_rows(f, b, "eval")
        all_rows.extend(rows)
        measured.append({"frames": f, "block": b, "mode": "eval",
                         "total_ms": median(rows, "total_ms"),
                         "kernel_ms": median(rows, "kernel_ms"),
                         "pack_ms": median(rows, "pack_ms")})
    generations = []
    for f, b in dict.fromkeys([(128, 256), selected]):
        rows = read_rows(f, b, "generation")
        all_rows.extend(rows)
        for mode in ["off", "on"]:
            subset = [r for r in rows if r["mode"] == mode]
            generations.append({"frames": f, "block": b, "mode": mode,
                                **{k: median(subset, k) for k in
                                   ["total_ms", "eval_ms", "repro_ms", "kernel_ms", "pack_ms", "decode_ms"]}})
    (HERE / "measurements.json").write_text(json.dumps(all_rows, indent=2) + "\n")
    fields = ["frames", "block", "mode", "total_ms", "eval_ms", "repro_ms", "kernel_ms", "pack_ms", "decode_ms"]
    with (HERE / "summary.csv").open("w") as out:
        writer = csv.DictWriter(out, fieldnames=fields)
        writer.writeheader()
        writer.writerows(measured + generations)
    original_on = next(r for r in generations if r["frames"] == 128 and r["mode"] == "on")
    tuned_on = next(r for r in generations if (r["frames"], r["block"]) == selected and r["mode"] == "on")
    target = cpu / 40
    lines = [
        "# 固定 population=1024、cases=1024：ASGP 1T 比較與 GPU 調校", "",
        f"最佳評分設定為 frames={selected[0]}、blocksize={selected[1]}。完整一代 overlap on 為 **{tuned_on['total_ms']:.2f} ms**，"
        f"相對先前 ASGP 單執行緒 {cpu:.2f} ms 為 **{cpu/tuned_on['total_ms']:.3f}×**；尚未達到 40×。", "",
        "這是同一批初代程式的系統時間比較；ASGP/GAGP 數值與 reproduction 規則的差異沿用[原實驗說明](../asgp-quick-20260929/README.md)。"
        "不是相同演算法的純 GPU 加速比，也不是搜尋到解的速度。", "",
        "## 完整一代：評分＋產生子代", "",
        "ASGP 1T 沿用原實驗 3 次正式量測中位數；GAGP 原設定與調校設定都在本次重測。每種模式 1 次暖機、3 次正式量測，off/on 次序輪替。", "",
        "| Frames | Block | Overlap | 整代 ms | 相對 ASGP 1T | Evaluation ms | Reproduction ms |",
        "| --- | --- | --- | --- | --- | --- | --- |",
    ]
    for r in generations:
        lines.append(f"| {r['frames']} | {r['block']} | {r['mode']} | {r['total_ms']:.2f} | {cpu/r['total_ms']:.3f}× | {r['eval_ms']:.2f} | {r['repro_ms']:.2f} |")
    lines += ["", "Phase 計時可能重疊；不能把 evaluation 與 reproduction 相加當作整代時間。",
              f"調校後 overlap on 比本次原設定改善 {original_on['total_ms']/tuned_on['total_ms']:.3f}×。", "",
              "## 14 組設定：prepared evaluation", "",
              "同一批已編譯程式與已初始化 cases/session。包含 program packing、上傳、kernel、回傳及每次必要清理；不含 AST 編譯或 reproduction。",
              "每個設定先與原設定核對全部 1024 個 GPU aggregate fitness 完全一致；正式評分每次再核對。每組 1 次暖機、3 次正式量測，表列中位數。", "",
              "| Frames | Block | Eval ms | Kernel ms | Packing ms |",
              "| --- | --- | --- | --- | --- |"]
    for r in measured:
        lines.append(f"| {r['frames']} | {r['block']} | {r['total_ms']:.2f} | {r['kernel_ms']:.2f} | {r['pack_ms']:.2f} |")
    lines += ["", "## 為何調整 frames", "",
              "原設定 frames=128、block=256 的 nsys capture 顯示 fitness kernel 啟動 62 blocks、每 block 256 threads、每 thread 64 registers；"
              "4 次 kernel 平均 669.09 ms，佔 capture 中 GPU kernel 時間 99.9%。這個 capture 僅供診斷，不混入正式 timing。", "",
              "Runtime 對 region workspace 設定 512 MiB 上限，DRegionFrame 為 264 bytes。工作區大小會限制同時執行的 program blocks。"
              "frames=50、block=512 時依同一公式容納 79 blocks；這是根據程式公式計算，並非新 profiler 量測。", "",
              "本題輸入長度 1–50，DC skeleton 在長度 ≤1 時結束，每次 split 都限制在內部切點、子 list 嚴格縮短，phase grammar 不允許額外遞迴。"
              "因此本工作負載最多需要 50 個 live frames。修改初代及 mutation grammar 的 frame 上限，不截短資料、不更換程式，也不降低 fuel。"
              "這個界限僅適用本題與本份 grammar，不能直接套用到其他 DC/DP 工作負載。", "",
              "## 距離 40×", "",
              f"ASGP {cpu:.2f} ms ÷ 40 = **{target:.2f} ms/代**。目前最佳 overlap on 仍需再快 **{tuned_on['total_ms']/target:.2f}×**。"
              "現有結果不支持單靠 blocksize、frame 上限或 overlap 達成目標。", "",
              "下一階段應檢查 DC interpreter 的 phase 執行與 region memory 存取，以及 host 端的 program packing、reproduction 準備與子代 decode。"
              "這些是後續優化方向，尚未實作或證明能達到 40×；不能把不同工作負載或 kernel-only 倍率拿來填補整代差距。", "",
              "## 重跑與證據", "",
              "本次僅新增 benchmark harness，沒有修改 production runtime。硬體與 Release libraries 同原實驗：i9-10900K、RTX 3090、CUDA 12.6、sm_86。"
              "其餘設定沿用原實驗：seed=0、fuel=2,000,000、penalty=1000、tournament=2、mutation=0.3、subtree mutation=1、單代且不評分子代。", "",
              "```bash", "bash benchmarks/asgp-tuning-20260929/build.sh",
              "python3 benchmarks/asgp-tuning-20260929/run.py",
              "python3 benchmarks/asgp-tuning-20260929/report.py", "```", "",
              "run.py 保留既有完整結果、拒絕未完成或 identity 不符的結果。全新量測需使用新的輸出目錄；同時需要原實驗產生的 frozen inputs。"
              "選出 14 組中評分中位數最快者後，再與原設定比較整代時間；這是單一 seed 的小型探索，未估計跨 seed 變異。", "",
              "- [所有設定、命令與 artifact hashes](manifest.json)",
              "- [逐次量測](measurements.json)、[中位數 CSV](summary.csv)",
              "- [獨立 C++ replay harness](replay.cpp)、[重跑腳本](run.py)",
              "- [Profiler 命令與摘要](profile.json)、[驗證記錄](validation.txt)",
              "- 原始 CSV、parity log 與 nsys capture：../../logs/asgp-tuning/（本機保留）",
              "- [HTML 報告](index.html)", ""]
    limits_path = HERE / "limits-audit.json"
    if limits_path.exists():
        limits = json.loads(limits_path.read_text())
        lines += ["## 程式大小與執行限制核對", "",
                  "原本的 physical depth=48 是容許上限，不是初代實際深度。初代全部由 ASGP 同一批 expression trees 轉接；未另外生成深度 48 的 parent。", "",
                  "| 初代實際大小 | ASGP | GAGP physical AST |",
                  "| --- | --- | --- |",
                  f"| Nodes 中位數 | {limits['asgp_total_phase_nodes']['median']:g} | {limits['gagp_nodes']['median']:g} |",
                  f"| Nodes 最大值 | {limits['asgp_total_phase_nodes']['max']} | {limits['gagp_nodes']['max']} |",
                  f"| Depth 最大值 | {limits['asgp_max_phase_depth']['max']}（每 phase） | {limits['gagp_physical_depth']['max']}（含實體骨架） |", "",
                  "GAGP 多出的節點含 DC skeleton、Program 包裝與算子轉譯輔助節點。子代的容許搜尋空間仍未對齊："
                  "GAGP physical 1024/48 比目前 parents 所需更寬，會影響 mutation donor 與 reproduction。上述初代核對不代表子代已公平。", "",
                  "以本份 DC skeleton 與長度 ≤50 的 cases，可保守界定最多 99 個 visited frames、50 個 live frames。"
                  f"初代每 case 的預算保守上界為 ASGP {limits['asgp_steps_upper_bound_per_case']['max']:,} steps、"
                  f"GAGP {limits['gagp_fuel_upper_bound_per_case']['max']:,} fuel，皆小於 2,000,000。"
                  "這是靜態上界，不是實測消耗，也不代表兩種計數單位等價；它排除了本批初代因 step/fuel 預算耗盡而提前終止的情況。", "",
                  "ASGP recursion 從根 depth=0 起算；max_recursion=128 允許最多 129 個 live frames，GAGP frames=128 允許 128 個。"
                  "要對齊 128 個 live frames 應使用 ASGP max_recursion=127、GAGP frames=128；若採本題所需的 50 frames，則為 ASGP 49、GAGP 50。"
                  "本次歷史量測仍保留原設定，未冒充已重跑的共同限制基準。", "",
                  "後續共同限制基準須在 crossover/mutation 後約束每 phase 的來源語言深度 ≤7，另列 physical AST 統計；"
                  "不能直接把 GAGP physical depth 改成 7。若要比較 timeout 行為，還須對齊語意事件計數，不能只把 step/fuel 設成相同數字。", "",
                  "重現核對：`bash benchmarks/asgp-tuning-20260929/audit_limits.sh`。",
                  "[逐項統計與假設](limits-audit.json)、[C++ 靜態核對](audit_limits.cpp)、[統計腳本](audit_limits.py)。", ""]
    (HERE / "README.md").write_text("\n".join(lines))
    def table(headers, rows):
        cells = lambda row, tag: "".join(f"<{tag}>{html.escape(str(v))}</{tag}>" for v in row)
        return "<table><thead><tr>" + cells(headers, "th") + "</tr></thead><tbody>" + "".join(
            "<tr>" + cells(row, "td") + "</tr>" for row in rows) + "</tbody></table>"

    generation_table = table(["Frames", "Block", "Overlap", "整代 ms", "相對 ASGP 1T"], [
        [r["frames"], r["block"], r["mode"], f"{r['total_ms']:.2f}", f"{cpu/r['total_ms']:.3f}×"]
        for r in generations])
    eval_table = table(["Frames", "Block", "Eval ms", "Kernel ms", "Packing ms"], [
        [r["frames"], r["block"], f"{r['total_ms']:.2f}", f"{r['kernel_ms']:.2f}", f"{r['pack_ms']:.2f}"]
        for r in measured])
    pages = [
        f'<section><h1>ASGP 1T 與 GAGP GPU</h1><p>Population = 1024；cases = 1024</p>'
        f'<p>ASGP 單執行緒：{cpu:.2f} ms／代。</p>'
        f'<p>調校後 GPU overlap on：{tuned_on["total_ms"]:.2f} ms／代，<strong>{cpu/tuned_on["total_ms"]:.3f}×</strong>。</p>'
        f'<p>40× 需要 {target:.2f} ms／代；目前仍須再快 {tuned_on["total_ms"]/target:.2f}×。</p>'
        '<p>同一批初代程式的系統成本比較，並非完整搜尋或相同演算法的純 GPU 加速比。</p></section>',
        '<section><h2>完整一代：評分＋產生子代</h2>' + generation_table +
        '<p>原設定與調校設定均重新量測；每種模式 1 次暖機、3 次正式量測，中位數。</p>'
        '<p>ASGP 1T 沿用原實驗中位數。Off/on 次序輪替；不計下一代評分。</p></section>',
        '<section><h2>全部 14 組設定：prepared evaluation</h2>' + eval_table +
        '<p>每組 1 次暖機＋3 次正式量測。全部 1024 個 GPU aggregate fitness 與原設定完全一致。</p>'
        '<p>包含 packing、上傳、kernel、回傳與清理；不含 AST 編譯或 reproduction。</p></section>',
        '<section><h2>瓶頸與調校依據</h2>'
        '<p>Nsys：原設定 fitness kernel 平均 669.09 ms，佔 GPU kernel 時間 99.9%；此 capture 不混入正式 timing。</p>'
        '<p>512 MiB region workspace 限制同時執行的 blocks。原設定 62 blocks；調校設定依程式公式可容納 79 blocks。</p>'
        '<p>輸入長度 1–50；DC 每次 split 都嚴格縮短 list，長度 ≤1 結束。故本題最多需要 50 個 live frames。</p>'
        '<p>僅調整 frame 上限與 block size。程式、cases、fuel 固定，production runtime 未修改。</p>'
        '<p>下一步：DC interpreter、region memory 存取與 host packing／reproduction／decode；尚未證明可達 40×。</p></section>',
        '<section><h2>實驗設定與範圍</h2>'
        '<p>i9-10900K、RTX 3090、CUDA 12.6、Release sm_86、GPU device 0。</p>'
        '<p>Sum of Elements；初代 seed 0、data seed 20260626；fuel 2,000,000、penalty 1000；tournament 2、mutation 0.3。</p>'
        '<p>ASGP cache、early stop、simplification 關閉。GAGP 使用 GPU evaluation 與 GPU reproduction。</p>'
        '<p>ASGP/GAGP 數值與 reproduction 規則仍有差異；沿用原實驗，未宣稱數值等價。單一 seed 的探索性比較。</p>'
        '<p><a href="README.md">完整說明</a> · <a href="manifest.json">設定與 hashes</a> · '
        '<a href="measurements.json">逐次量測</a> · <a href="summary.csv">CSV</a> · '
        '<a href="../asgp-quick-20260929/index.html">原 ASGP 比較</a></p></section>',
    ]
    if limits_path.exists():
        pages.append('<section><h2>程式大小與限制：補充核對</h2>'
                     '<p>初代實際最大：ASGP 664 nodes／phase depth 7；GAGP 707 nodes／physical depth 11。GAGP 未長到原上限 48。</p>'
                     '<p>GAGP 子代搜尋限制仍較寬，不能以初代已對齊推論 reproduction 也公平。</p>'
                     '<p>初代每 case 的保守預算上界：ASGP 65,736 steps、GAGP 75,540 fuel，皆遠低於 2,000,000；計數單位仍不同。</p>'
                     '<p>相同 128 live frames：ASGP max_recursion=127、GAGP frames=128。原量測尚未改成這組共同限制。</p>'
                     '<p>後續應限制每 phase 的來源語言深度 ≤7，並核對 crossover/mutation 後的子代。</p>'
                     '<p><a href="limits-audit.json">完整核對數據</a> · <a href="README.md">說明與重現方法</a></p></section>')
    (HERE / "index.html").write_text(
        '<!doctype html><html lang="zh-Hant"><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width,initial-scale=1">'
        '<title>1024 × 1024：ASGP 比較與 GPU 調校</title>'
        '<style>body{background:white;color:black;margin:32px;font:20px/1.6 sans-serif}'
        'section{max-width:1100px;min-height:85vh;margin:0 auto 48px}'
        'table{border-collapse:collapse;font-size:18px}th,td{border:1px solid black;padding:4px 14px}'
        'a{color:black}@media print{@page{size:A4 landscape;margin:15mm}'
        'body{margin:0;font-size:17px}section{min-height:0;break-after:page;margin:0}'
        'section:last-child{break-after:auto}table{font-size:14px}th,td{padding:2px 12px}}</style>'
        '<body>' + "\n".join(pages) + '</body></html>')
    print(f"ASGP 1T {cpu:.3f} ms; tuned GPU on {tuned_on['total_ms']:.3f} ms; {cpu/tuned_on['total_ms']:.3f}x")
    print(f"Validated {len(all_rows)} rows: {len([r for r in all_rows if int(r['rep']) >= 0])} formal runs")


if __name__ == "__main__":
    main()
