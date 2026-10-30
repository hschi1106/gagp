# Customizable Grammar report

Open [index.html](index.html) in a browser. The report contains 25 English pages
with a white background, black text, built-in structure and flow diagrams, and no
external dependencies.

Use Previous / Next, the arrow keys, or the page selector. **Read all** enables
continuous reading; **Slide view** returns to single-page navigation. Direct links
such as `index.html#slide-21` open a specific page. Without JavaScript, all pages
remain readable. Printing includes every page; select landscape, enable background
graphics, and disable browser headers and footers.

The report follows one beginner-friendly worked example: discovering a program
that doubles an integer. It introduces building blocks, rules, program trees,
fitness and types before showing JSON. Later pages explain lists, traversal,
templates, packages, bounded recursion and GPU storage through concrete examples.
Advanced search controls appear only after the first executable workflow.

[double.cases.json](double.cases.json) contains the four input/output pairs used by
the report's commands. These intentionally differ from the repository scalar
example's identity cases. Performance measurements are outside the report.

This is explanatory material for the 2026-09-29 workspace, not a separate source of
semantic authority. Follow the [language specification](../../../spec/grammar.md),
[authoring schema](../../../spec/grammar_definition.md),
[authoring guide](../../guides/grammar-authoring.md),
[bytecode contract](../../../spec/bytecode_isa.md), and
[payload design](../../design/payload.md) for maintained contracts.

## Historical benchmark attachments

The following records and scripts belong to the earlier performance presentation.
They are retained as historical evidence and are not required to read or use the
rewritten report. References below to slides or result tables describe that earlier
presentation.

## 執行主矩陣

從 repository root 執行，需要 CUDA toolchain 與可用 GPU：

```bash
cmake -S cpp -B cpp/build/release -DCMAKE_BUILD_TYPE=Release -DGAGP_ENABLE_CUDA=ON \
  -DCMAKE_CUDA_COMPILER=/usr/local/cuda-12.6/bin/nvcc -DCMAKE_CUDA_ARCHITECTURES=86
cmake --build cpp/build/release -j
mkdir -p logs
GAGP_CUDA_DEVICE=0 bash docs/presentations/customizable-grammar/run-matrix.sh
```

[run-matrix.sh](run-matrix.sh) 依序執行四個 population size、四種模式、三個 seed，
每組一次暖機與五次正式量測；共 288 次 evolution process，另有 population generation/replay。
各模式共用相同初代；順序輪替，單次量測不平行執行。請在同一台、無其他 GPU 負載的環境量測。
腳本不會自行編譯、不會覆寫既有輸出資料夾；可用第一個參數指定新的輸出路徑（父目錄須存在）。

輸出在 ignored `logs/grammar-slides-*`：完整 CLI、環境、hashes、population、raw JSON、stdout/stderr 與 `summary.csv`。
預設保留所有 artifacts，執行時間與磁碟用量視主機而定。失敗時停止，不把缺失結果當成零。

[summarize.py](summarize.py) 可獨立重新彙整：

```bash
python3 docs/presentations/customizable-grammar/summarize.py logs/grammar-slides-YYYYMMDD-HHMMSS
```

每個 seed 先取五次 duration 的 median，再算配對加速比，最後跨 seed 報 median/min/max。CSV 同時提供 evolution、generation、fixed_pop 與 process 四種 scope。
`median_ms` 是三個 seed median 的中位數；speedup 是配對比值的中位數，兩者不可混用。
`fixed_pop` 的 GPU duration 加上 `gpu_eval_init_ms`；GPU reproduction 的 first-touch 可能先在資源建立時發生，因此此欄位不能代表完整冷啟動。主表改採 `meta.timing.total_ms`，同時列出單代時間。`process` 另記 `/usr/bin/time` 的完整 CLI elapsed time，但輸出解析度為 10 ms，僅供輔助。新 process 暖機不會消除下一次 process 的 CUDA 初始化。
CSV 是填入投影片結果表的依據，不會自動修改投影片。

## 驗證範圍

投影片 JSON 與現有 scalar 範例一致；附件可做 shell 語法與合成 timing fixture 彙整檢查。
Release / CUDA sm_86 build 成功；grammar reproduction、GPU smoke、fitness CPU/GPU parity、evolution CPU/GPU parity 共 4 項通過。初次使用 repository 預設 sm_89 時無法在 RTX 3090 執行 fitness kernel，改用 sm_86 後才開始正式量測。240 筆正式結果的設定、grammar identity 與配對初代 history 已驗證。

## 實測結果

- [完整設定與 hashes](../../../benchmarks/customizable-grammar-20260929/manifest.json)
- [四種計時範圍的 CSV](../../../benchmarks/customizable-grammar-20260929/summary.csv)
- [240 筆 timing 與結果 hash](../../../benchmarks/customizable-grammar-20260929/measurements.json)
- [環境](../../../benchmarks/customizable-grammar-20260929/environment.txt)、[驗證記錄](../../../benchmarks/customizable-grammar-20260929/validation.txt)、[完整命令](../../../benchmarks/customizable-grammar-20260929/commands.sh)
- [原始 artifacts](../../../logs/grammar-slides-20260929/)（ignored，保留於本 workspace）

主表是完整演化引擎計時：含 backend 啟動與 population 初始化，不含 CLI 載入、序列化與函式返回後解構。

| N | CPU ms | GPU eval ms / speedup | GPU repro ms / speedup | GPU overlap ms / speedup | Overlap C÷D |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1024 | 170.839 | 183.332 / 0.934× | 170.133 / 1.012× | 164.654 / 1.040× | 1.033× |
| 2048 | 351.614 | 243.643 / 1.447× | 211.776 / 1.660× | 200.387 / 1.755× | 1.057× |
| 4096 | 706.190 | 367.815 / 1.922× | 299.751 / 2.340× | 278.541 / 2.525× | 1.077× |
| 8192 | 1391.172 | 618.035 / 2.251× | 652.274 / 2.133× | 595.313 / 2.336× | 1.095× |

跨 seed 的配對加速比範圍（min–max；不是信賴區間）：

| N | B | C | D | Overlap C÷D |
| --- | ---: | ---: | ---: | ---: |
| 1024 | 0.931–0.951× | 1.000–1.025× | 1.036–1.058× | 1.029–1.035× |
| 2048 | 1.439–1.471× | 1.651–1.696× | 1.754–1.770× | 1.044–1.062× |
| 4096 | 1.841–1.951× | 2.337–2.358× | 2.521–2.535× | 1.075–1.080× |
| 8192 | 2.232–2.289× | 2.132–2.139× | 2.335–2.346× | 1.092–1.100× |

原始結果沒有刪除 outlier。各組先暖機一次，正式模式次序輪替且不平行執行。顯示卡仍有桌面圖形程序，因此不宣稱完全隔離環境。這是相對本專案循序 CPU 的加速，不是相對最佳化多核心 CPU；結果限於 simple_exp grammar 與 1024 cases。

8192 的單代 GPU reproduction 成本出現非線性增幅：C 模式相對 4096，preprocess 從 45.92 ms 增至 144.68 ms，decode 從 22.76 ms 增至 144.93 ms；GPU reproduction kernel 仍僅約 0.60 ms。這支持主要增幅位於 host 階段的判斷，但本次沒有用 profiler 隔離底層原因。最大 speedup 出現在 4096：完整演化 D 為 2.525×，單代 D 為 4.935×；本次未量測多代收斂或成功率。

## 與歷史 30× 結果的核對

2026-09-29 已 SSH 讀取 Frieren（port 24090）上的 Codex 原始對話與封存 receipts。那次 Codex 透過 port 3090 在 Snoopy 執行 benchmark，亦即本次使用的 i9-10900K / RTX 3090；不是 4090 與 3090 的硬體差別。

- 原對話 `01a08f80-17bc-7393-a3ff-b3ae3de49f8c`，2026-09-27 08:10 UTC 回報 30.28×，10:12 UTC 回報後續 31.44×／30.44×。
- 已核對後續兩組共 12 份 CPU/overlap 正式 receipt 與輸出 SHA-256；重新計算為 5236.191683 / 166.549429 = 31.439265×、5211.444841 / 171.205086 = 30.439778×。
- 歷史設定是凍結 migration population、216 個 nonterminals、17 個 templates、fuel 20000、source 80 nodes/depth 7 投影至 physical 531/141，並在 CPU/GPU 同用 tcmalloc。
- 本次是新生成的 scalar population、1 個 nonterminal、0 templates、fuel 256、physical 31/8、預設 allocator。兩組都叫 simple_exp 且有 1024 cases，不代表相同程式工作量。
- 歷史 receipts 還顯示 CPU 與 GPU 各自使用 input grammar／gpu-proposal-profile；本次主矩陣四模式則共用同一 grammar。歷史結果不能當作目前同 grammar 基準的直接比較。

因此，歷史 30× 有實驗證據，本次 4.29× 也是另一工作負載的量測；本次矩陣並沒有重現歷史 benchmark，不能据此宣稱效能退步。先前報告未把這個差別說明清楚，已補上第 23 頁。

[原對話摘錄、核對 receipts 與設定比較](../../../benchmarks/customizable-grammar-20260929/historical-audit.json)；[歷史正式報告](../../reference/grammar-migration/goal-11.5-target35.md)。未修改遠端對話或實驗檔案，也未重新執行歷史 benchmark。
