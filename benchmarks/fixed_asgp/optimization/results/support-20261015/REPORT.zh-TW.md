# ASGP 30 題支援、fitness 對齊與低預算比較 — 2026-10-15

這輪完成實作、逐題驗證及低預算工具。**不是 30 題都已公平對齊，也不是全部 GPU 都已具加速優勢。**
三個 Astra-medium subagents 分工處理 source grammar、fitness／elitism、payload／memo；root 整合及執行串行 GPU 檢查。

- 30／30 題可準備 P=1024 的完整 population；這是 admission 證據，並非 30 題 P=1024 搜尋完成的宣稱。
- 30 題 × 五模式的 P=8、完整 1024 cases、兩代搜尋全部完成，150／150 通過執行檢查，其中 120 個 GAGP 搜尋的 final CPU audit 均通過。
- 29／30 已知解通過；24／30 source sampled-output/status 對照通過；29／30 CPU/GPU 初始 snapshot 通過；合併嚴格 gate 為 **23／30**。
- Sum／House／Median 的 P=1024、完整 1024 cases、3 seeds × 2 generations × 五模式共 45 次搜尋及 45 次 heldout audit 通過。
- 未執行正式九次 repeats、長程收斂實驗、完整 scaling matrix 或昂貴 profiler。

## 逐題結果

「通過」只代表本次有限程式／案例的嚴格 gate；不等於窮盡所有 grammar 程式。所有列的 P=1024 admission 與 P=8 五模式執行都通過。

| 題目 | 嚴格 gate | 剩餘限制 |
|---|---|---|
| Filter Odd | 未通過 | ASGP／CPU 資源失敗狀態不同 |
| Sum of Elements | 通過 | — |
| Is Sorted | 通過 | — |
| Index of Element | 通過 | — |
| All Equal | 通過 | — |
| Cumulative Sum | 未通過 | 目前 source grammar 無一般 prefix-sum witness，且資源狀態不同 |
| Fuel Cost | 通過 | — |
| Luhn | 通過 | — |
| Min | 通過 | — |
| MPI | 通過 | — |
| Count Odds | 通過 | — |
| Last Index of Zero | 通過 | — |
| Small or Large | 通過 | — |
| For Loop Index | 未通過 | ASGP／CPU 資源失敗狀態不同 |
| Double Letters | 未通過 | ASGP／CPU 資源失敗狀態不同 |
| Replace Space with Newline | 未通過 | ASGP／CPU 資源失敗狀態不同 |
| Even Squares | 未通過 | ASGP／CPU 資源失敗狀態不同 |
| Sum of Squares | 通過 | — |
| Negative to Zero | 未通過 | 初始 GPU／CPU snapshot 有 2 個體不一致；已精確定位 arena 上限 |
| Scrabble Score | 通過 | 已知解 witness phase depth=16，超過 source 搜尋 depth=7；只證明執行能力 |
| Grade | 通過 | — |
| Median | 通過 | — |
| Smallest | 通過 | — |
| Syllables | 通過 | — |
| Fibonacci | 通過 | — |
| Tribonacci | 通過 | — |
| House Robber | 通過 | — |
| Subregion Sum | 通過 | — |
| Fibonacci Array | 通過 | — |
| Stock with Cooldown | 通過 | — |

原始逐題 outcome、每模式狀態、generation counters 見 [support/tasks.csv](support/tasks.csv)、[support/searches.csv](support/searches.csv)、[support/generations.csv](support/generations.csv)。固定程式對照見 [task-audits.json](task-audits.json)，P=1024 admission 與實體節點上限見 [capacity.json](capacity.json)。

## 真正對齊與尚未對齊的部分

新增 `FitnessPolicy`：Absolute、Exact、Levenshtein、TupleSummary，以及 capped／discounted proximity、summary count／weight／cap。原生 CPU 與 GPU 在 payload 存活期間計分，保留 wrong-case count；錯誤依該 task 的 ceiling 處理。Tuple 的 wrong count 只看 predicate，summary 參與 error；畸形 tuple 必須拒絕或完整計為錯誤。
`GAGP_BM_ALIGNED_FITNESS=1` 從 ASGP task metadata 選擇策略，並設定一個 elite。搜尋以 error、wrong、size 排序；原生 ranking 仍有 40-bit canonicalization，size 仍是 native AST size。對齊模式拒絕與 ASGP cap=1000 不一致的 penalty override。歷史 Legacy 策略、無 elite 預設及 frozen 路徑仍保留。
初始 source programs、完整 cases、production／constant support 可共用；Char／CharList 仍保留不同 nonterminal，tuple 是受控 IntList encoding。新增 `trunc_div0`／`trunc_mod0`，對應帶零除與 INT64_MIN/-1 保護的截斷整數運算；既有 `idiv0`／`imod0` 語意不變。修正 generic／typed CPU/GPU 的 int64 低位精度，並保留 source 負 index 的錯誤語意。
**搜尋契約未完全相同**：ASGP crossover gate 0.7，native 每 child 嘗試 crossover；phase/site sampling、terminal/grow、constant-range sampling、logical phase depth、實體容量、fuel、arena、RNG、elite 先產生再覆蓋的工作量都有明列差異。ASGP source depth=7 與 native depth=48／nodes=1024 不是相同 bounded search space。完整 named audit 見 [search-contract.json](search-contract.json)；`fully_aligned` 保持 false。

## 資源與正確性修復

- CPU fitness 每 case 使用 discard-only 8 MiB payload scope，計分後釋放暫存；同一 program 重用 overlay metadata。這消除了跨 case 不斷保留暫存的來源。未用 before/after 精確量測宣稱 metadata 重用的速度收益。
- GPU ExtendedMixed profile 容量為 8192 list values／8192 string bytes、各 1024 entries；超限回傳明確 Value error。scalar/view profile 不共同承擔此 arena。GPU memo 宣告上限 256 cells，配置仍依實際宣告需求，frame 上限未提高。
- Memo subordinate expression 只有在前一排名座標已證明嚴格下降、duplicate policy 適用時可接受；完整 memo key 仍含全部相關座標。Subregion 的 reachable cell bound=20；Fibonacci Array 保留 169-cell 支援。
- Negative to Zero 已知解的長度 50 × 1024 cases 現在通過；隨機搜尋 P8G2 在 2 GiB 限額下完成。但兩個初始個體的重複 concat 分別在 n≥43、n≥49 耗盡 GPU arena，精確重現 164／25 errors，以及 CPU/GPU fitness 差距。證據：[negative-capacity-diagnosis.json](negative-capacity-diagnosis.json)。沒有以關掉檢查掩蓋差異。
- Cumulative Sum 限制針對這份 ASGP source checkout：combine 的 list language 只有子結果／concat，缺少一般 prefix scan 所需的跨子結果轉換。未注入 solver，未擴張 source 語言假稱對齊。

## 短程品質

P=1024、完整 1024 cases、三個 seeds、兩代。下表為另以固定獨立 seed 生成 1024 heldout cases 的 exact accuracy 中位數；不是官方 PSB test set，自然重複與 training overlap 沒有人工排除。

| 模式 | Sum | House | Median |
|---|---:|---:|---:|
| ASGP 1T | 100.00% | 26.76% | 35.25% |
| GAGP CPU | 100.00% | 26.76% | 35.25% |
| GPU-e | 100.00% | 26.76% | 35.25% |
| GPU-er | 100.00% | 26.76% | 35.25% |
| GPU-er-o | 100.00% | 26.76% | 35.25% |

沒有看到明顯退化，但證據很弱：Sum 第零代已全對；Median 各模式兩代內最佳 training score 都維持 -642。House 的 ASGP／CPU／GPU-e seed1 改善 training error 8503→8479，heldout capped L1 mean 8.241→8.224，但 exact accuracy 26.76%→4.98%；GPU-er／o 仍維持原 champion。這顯示 loss 與 exact accuracy 的取捨，不能只看某個 accuracy 中位數宣稱等價或勝出。
所有 heldout champion 的 runtime errors=0；native export 經獨立 grammar admission／CPU 重評。P1024 final audit 只覆蓋 16 個體，不是全 population 重評；P8 support final audit 覆蓋全部 8 個體。原始 45 組品質數值見 [quality-per-seed.csv](quality-per-seed.csv)。未測長程品質、達標率或長跑記憶體趨勢。

## 一次性低預算 timing

每模式一個 process、seed0、P=1024、完整 1024 cases、兩個完整 generations，加上必要初始化與 final evaluation。下面是 `evolve_wall_ms` 秒數；輸入解析、外部準備、CPU audit／JSON 輸出另見 process 欄位，不能混稱全部 CLI wall。沒有 profiler／多次 repeats，僅作粗略優化回饋。

| 模式 | Sum | House | Median |
|---|---:|---:|---:|
| ASGP 1T | 6.068 s | 3.053 s | 0.171 s |
| GAGP CPU | 20.500 s | 8.111 s | 1.453 s |
| GPU-e | 1.089 s | 0.943 s | 0.764 s |
| GPU-er | 1.152 s | 0.521 s | 0.769 s |
| GPU-er-o | 1.074 s | 0.518 s | 0.779 s |

同輪 ASGP／最快 GPU 的粗略比值約 5.65×／5.89×／0.22×；**Median 的 GPU 比 ASGP 慢**。這不是舊的 frozen grammar、32／64 代搜尋或舊 ASGP 分母，不能沿用舊 25×／30× 宣稱。所有原始單次資料與兩代分項見 [benchmark/searches.csv](benchmark/searches.csv)、[benchmark/generations.csv](benchmark/generations.csv)；完整 binary／settings／input identities 在各 stage manifest。
新 source grammar 的 Sum／Median 使用 `native-ast` GPU reproduction fallback，仍然在 GPU 做 variation，但 host preparation／decode 很重；House 可使用 `native-gpu-phase-v2-counted`。本次 Sum 每代 repro 約 222–238 ms，其中 decode 約 102–118 ms；Median repro 約 211–224 ms、decode 約 121–127 ms；House repro 約 3.3–4.0 ms。這些是內建診斷欄位，不能從有 overlap 的總時間直接逐項相減。
優先下一步是讓 repeated-hole guard／必要 capture 等 source grammar 構造進入既有精簡 GPU phase 編譯與 variation 路徑，維持 grammar membership 與負 index 語意；其次才是 list arena liveness／回收及 CPU payload scoring。不能為重得快路徑而移除 source guard，也不能把 reproduction 搬回 CPU。

## 成本、測試與重現

最終 support 348.4s；P1024 admission 合計約 140.1s；quality 197.6s；一次性 benchmark 69.1s。這是最終階段成本，不含前面失敗原型／修復重跑／build 時間。所有階段都有 timeout／RSS／總 wall budget；無高成本完整精確 benchmark。
原生 128 項測試覆蓋：126 項初次通過，兩項修正後補測通過。exposed-index 測試改為檢查正確 int64 低位；legacy migration 保持六份原始 golden grammar 不變，排除新增運算對歷史 preset 的影響。另三項 focused external regressions 通過；tools 100 項（4 項既有 skip）、repository 24 項通過。測試紀錄見 [tests/](tests/)。完整 build 成功。
未跑 optional external 的整套 fixed-parent repeats 測試；本輪用 150 個五模式 smoke 與三個 focused regressions 替代該完整入口。沒有刪除安全測試或以 expected-failure 掩蓋新錯誤。

```bash
PYTHONPATH=tools python3 -m gagp_tools.experiments.fixed_asgp.low_budget support --out logs/support-NEW --budget 600 --timeout 25
PYTHONPATH=tools python3 -m gagp_tools.experiments.fixed_asgp.low_budget quality --artifacts logs/support-NEW --out logs/quality-NEW --budget 300 --timeout 25
PYTHONPATH=tools python3 -m gagp_tools.experiments.fixed_asgp.low_budget benchmark --artifacts logs/support-NEW --out logs/benchmark-NEW --budget 180 --timeout 30
```

每次用新目錄，三個 stage 依序跑。日常 patch 可用 `--tasks house_robber median --modes gpu_overlap` 縮小測試範圍；完整 cases 不變。設定完整保存在 [settings.json](settings.json)，CPU1T 明確固定，GPU 程序串行。原始 binary／full exports／每 case heldout 結果保留於 integration worktree 的 `logs/support-20261015/{native-support,quality,benchmark}`。外部 ASGP source 檔案 identity 見 [asgp-source-files.json](asgp-source-files.json)。

目前 runtime 功能 checkpoint 為 `e2da6ad`；後續 `a83c64a` 修診斷 observer、`4803efd` 修既有測試的錯誤數值期望、`8163e47` 保護 legacy migration。實際量測使用 manifest 內 archived binary，不能只憑 commit 名稱重建設定。整合分支 `feat/asgp-support-contract-20261015`；原始 checkpoint `dc99120` 與歷史 artifacts 不變。主 worktree 的未提交文件修改保持原樣；遠端 main 推送結果由交付訊息確認。

尚未完成：七題完整執行資源契約對齊、所有搜尋分布／深度契約一致、30 題 P1024 多代品質、長跑與全題 GPU 優勢。這些不是已通過項目。
