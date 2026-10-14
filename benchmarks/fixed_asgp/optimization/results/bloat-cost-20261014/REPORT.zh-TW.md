# Bloat 成本優化整合報告 — 2026-10-14

本輪由三個 Astra-medium 子代理在獨立 worktree 探索四個方向，主代理負責整合與採用。**House 32 代達 26.42×，尚未達到 30×；Median 64 代達 6.83×，超過 5×。CPU 仍遠慢於 ASGP。** 沒有更換 reproduction 架構、開 CPU 多執行緒、改 RNG／variation 規則或縮減評估工作。

## 版本與完整設定

最佳 runtime commit：`be455a4`，branch `opt/gpu-generation-20261001`。實驗起點／可恢復原始參考：`checkpoint/pre-bloat-cost-20261014`（`71dc916`）。原始 worktree、輸入、archived binaries 與歷史結果保留；本輪沒有 push。

完整設定、binary SHA、source 身分、frozen input hashes 與所有原始 repeats 見 [final.json](final.json)。需保留上一版全部 flags，再加：

```text
GAGP_GPU_DEFERRED_CROSSOVER_COPY=1
GAGP_GPU_FUSED_SPLICE_DEPTH=1
GAGP_GPU_COMPACT_COMPILE_FRAMES=1
```

CPU 限制仍為 1T。這三個開關只作用於原生 GPU reproduction；預設參考路徑保留。不新增 grammar／operator／數值／fuel profile，也不改原本 fast path 的支援範圍與 fallback。

## 完整搜尋結果

P=1024、完整 1024 cases；House seed0／32 代，Median seed2／64 代。時間包含冷啟動、必要 import/admission、完整 generations、最終 evaluation/export。正式時間未開 profiler。ASGP／CPU 實作與設定未變，表中搜尋 controls 明確沿用上一輪；GPU-e／GPU-er 本輪各 3 次，GPU-er-o 與 archived prior best 交錯各 9 次。所有原始資料另存，不覆蓋歷史。

| 模式 | House 32 代 | Median 64 代 | 搜尋證據 |
|---|---:|---:|---|
| ASGP 1T | 27.576167 s | 5.899860 s | 沿用 3 次 controls |
| GAGP CPU | 82.103390 s | 42.153159 s | 沿用 3 次 controls |
| GPU-e | 8.461391 s | 24.091612 s | 本輪 3 次 |
| GPU-er | 1.050854 s | 0.861684 s | 本輪 3 次 |
| GPU-er-o | 1.047911 s | 0.858233 s | 本輪 9 次 |

Headline 分母固定為 House **27687.368225 ms**、Median **5864.915193 ms**，不使用較有利的新分母。House 30× 需低於 **922.912274 ms**；目前仍需減少約 **125.0 ms／11.93%**。GPU-er 與 GPU-er-o 的細微差距不構成 overlap 的獨立收益證據。

同輪 before／after 完整搜尋中位數：

| 題目 | Archived prior best | 本輪候選 | 時間下降 |
|---|---:|---:|---:|
| house_robber | 1067.850610 ms | 1047.911452 ms | 1.87% |
| median | 914.303899 ms | 858.233274 ms | 6.13% |

九次原始完整搜尋時間（ms），含 cold start：

- house_robber before: 1128.486824, 1055.807948, 1060.970838, 1061.992724, 1076.267535, 1072.841131, 1080.282072, 1060.186615, 1067.850610。範圍 1055.808–1128.487。
- house_robber after: 1044.972976, 1049.233633, 1048.888957, 1042.525348, 1047.911452, 1051.000769, 1045.760212, 1066.507474, 1044.652362。範圍 1042.525–1066.507。
- median before: 911.176254, 907.966959, 920.394757, 910.302545, 920.542739, 911.075193, 914.303899, 923.572908, 918.066386。範圍 907.967–923.573。
- median after: 857.234398, 857.927035, 852.136587, 858.233274, 858.076152, 867.337306, 860.137451, 862.960415, 862.169294。範圍 852.137–867.337。

完整 CLI process 還包含輸入解析、獨立 audit、history／JSON 輸出。GPU-er-o process 中位數 House **2.289 s**、Median **2.037 s**；26.42×／6.83× 是上述固定搜尋邊界，不能宣稱為完整 CLI process 倍率。[generation-times.csv](generation-times.csv) 保存各模式逐代時間、fitness、大小與相關 counters，包含控制組是否沿用。

## 留下與淘汰的實作

| 方向 | 實作與結果 |
|---|---|
| 物理複製 | 保留 deferred crossover copy：成功替換的 phase 不再先複製父代後立即覆寫；其餘 phase 與重試失敗後的完整父代仍正確複製。沒有改邏輯 crossover sites／機率或 mutation。 |
| 局部 metadata 工作 | 保留 fused splice depth：建立輸出時同時做原本所需的 prefix arity／depth 檢查，取消成功輸出後第二次完整 depth traversal。不是省略驗證。 |
| GPU lowering 工作空間 | 保留 compact compile frames：複製整個 node 的 frame 改為索引 immutable 輸入與窄化私有暫存。stack/local storage 2816→768 bytes，registers 40 不變、spill loads/stores 都為 0。仍編譯完整變動 phase，沒有宣称 subtree incremental compilation。 |
| Executable CSE | 淘汰。即使維持精確 fuel／錯誤順序，House 128 代 kernel 約省 392 ms，packing 增加約 493 ms；Median 128 代整體約 3142→4125 ms。OFF binary 也有 codegen 成本，因此另與 archived baseline 比較。runtime／設定／專屬測試已撤除。 |
| Live workspace | 淘汰。移除未使用 memo backing 原本看似可省記憶體，實際引發每代配置 churn。即使改 bounded allocation reuse，House 仍 1074.895→1092.077 ms。沒有把 Median 的無關波動算成收益。 |

前三項合併的正式收益以上述交錯九次為準，單項百分比不能相加。Compact frame 的獨立短測收益小、容易受噪音影響；採用依據是合併版重複結果、較小工作空間與測試，而不是宣稱每個開關各自都有顯著收益。即使關閉新選項，編譯 helper 的 code generation 也有改變，因此 archived binary 是必要控制。

沒有引入 persistent DAG、跨代 subtree executable cache 或 CPU fragment backend。先前 selected-parent executable reuse 原型已因低命中率淘汰，這次不把它當新成果重做。物理 sharing 只保留已可證明安全的 node indexing／避免無效複製，沒有為「架構簡化」新增昂貴的通用 ownership framework。

證據：[incremental/sharing](incremental-sharing.json)、[rejected CSE](rejected-executable-cse.json)、[rejected workspace](../bloat-liveness-20261014.json)、[初始 screen](screen.json)。淘汰原型的 patch／raw logs 留在隔離實驗資料中，不保留執行入口。

## 剩餘成本

下列是最終同輪完整搜尋的內建分項計時中位數（ms），只作診斷。kernel／pack／upload 是外層 eval 或 repro 的子項，**不能重複相加**；不同分項中位數也不要求相加等於總中位數。

| 搜尋累計成本 | House32 | Median64 |
|---|---:|---:|
| Initial admission/import | 103.291 | 95.570 |
| All generations | 761.960 | 601.164 |
| Evaluation | 633.267 | 245.915 |
| Eval kernels（內含） | 577.804 | 157.669 |
| Reproduction | 125.853 | 349.082 |
| Repro kernels（內含） | 83.702 | 243.480 |
| Bytecode integration（內含） | 26.842 | 67.314 |
| Eval packing（內含） | 15.590 | 25.244 |
| Eval upload（內含） | 28.948 | 46.203 |
| Final evaluation/export | 38.486 | 14.160 |

House 的 eval 幾乎沒有變；主要節省 reproduction。Median reproduction kernel 累計降低，但仍是主要成本。內建計時以外的 setup／cleanup 仍包含在搜尋總時間，不把差額冒稱為精確 CUDA API attribution。

另做的 nsys 128 代診斷顯示，baseline compile kernel 約 House 427 ms／Median 366 ms，約占 GPU kernel 累計 4.3%／28.3%；這解釋了 House 為何只得到小幅收益。這些 profiled 數字不與正式未開 profiler 的時間混減。沒有使用 NCU。

## 品質、安全與長跑

127／127 native tests 通過，另重建並通過新增的 fused variation 邊界測試；24 repository checks 通過。新增測試包含深度／容量邊界、失敗後重用、compact frame fallback、獨立 CPU lowering／bytecode verifier 對照。錯誤輸出不會當成有效 offspring 發布，immutable 輸入的 lifetime 覆蓋 kernel。最終 CUDA 12.6 compute-sanitizer 對 phase compile、phase variation、phase population 三項檢查均為 **0 errors**；原始 log 保存在 validation 目錄。

18 組短搜尋 before／after、6 組 128 代 task／seed 比較，逐代 semantic counters、fitness、最終完整 AST 與 history 完全一致；history 只排除 elapsed timestamp。完整 cases 沒有 unscored，沒有新增 timeout／fallback。這是本輪語意維持證據，不是與 ASGP 收斂品質等價的證明。每次長跑獨立 CPU audit 是 16 個程式，不能冒稱全部 1024 個均 CPU 重評；既有 snapshot 的 House CPU／GPU 差異也沒有在這輪被修正。

| 題目／seed | 128 代完整搜尋 | 最終 fitness | 前16代單代中位數 | 後16代單代中位數 |
|---|---:|---:|---:|---:|
| house_robber / 0 | 11.548 s | -4427 | 20.926 ms | 171.181 ms |
| house_robber / 1 | 6.556 s | -865 | 24.044 ms | 71.230 ms |
| house_robber / 2 | 7.258 s | -4535 | 21.525 ms | 97.169 ms |
| median / 0 | 2.869 s | -478 | 11.052 ms | 27.469 ms |
| median / 1 | 1.535 s | -642 | 8.568 ms | 10.479 ms |
| median / 2 | 1.546 s | -642 | 8.657 ms | 11.253 ms |

長跑 bloat 沒有消失：House seed0 後段單代約為前段 8.18 倍。Median 存在原本軌跡的 runtime errors，與舊版完全相同，不能寫成零錯誤。六個長跑 process peak RSS 約 **1.18–2.28 GiB**，包含 history／AST／JSON；reported genotype device bytes 約 **0.54–0.74 GiB**，不等於完整 CUDA peak memory。沒有新增無界 cache，但這些結果也不能證明任意長跑記憶體固定。

## Scaling 與其他 workload

以下是 **fixed-parent 完整單代**，每格 warmup 一次後量測三次；不是前面的跨代搜尋。40／40 格完成且沒有 OOM，ASGP／CPU controls 全部本輪重跑。倍率使用每個 population 自己的 ASGP controls，不使用完整搜尋的 frozen 分母。

| Population | House GPU-er-o | 同格 ASGP 倍率 | Median GPU-er-o | 同格 ASGP 倍率 |
|---|---:|---:|---:|---:|
| 1024 | 31.210 ms | 41.92× | 6.768 ms | 8.43× |
| 2048 | 51.161 ms | 50.90× | 14.214 ms | 8.05× |
| 4096 | 100.004 ms | 52.03× | 24.217 ms | 9.39× |
| 8192 | 196.615 ms | 53.15× | 50.342 ms | 9.08× |

另有 3 processes × 3 measured repeats 的 P1024 fixed-parent formal：house_robber 中位數 31.117 ms（30.429–31.955）；median 中位數 6.654 ms（6.545–6.876）；完整 raw rows 位於 final.json。

Sum of Elements 的 P1024／C1024 五模式回歸完成，五種模式的單代 smoke checks 也全部完成。Sum fixed-parent 中位數（ms）：asgp_1t 1848.030、gagp_cpu 5555.407、gpu_eval 248.857、gpu_repro 45.976、gpu_overlap 46.221。這不是新增的 Sum 跨代搜尋品質實驗。

## 重現與續跑

所有 raw 結果：`logs/optimization/bloat-cost-20261014/validation/runs/`。完整 flags 的權威來源是 final.json／manifest，不只是上面的三個新開關。沿用現有 runners：

```bash
python3 benchmarks/fixed_asgp/optimization/results/bloat-cost-20261014/run_validation.py --out logs/optimization/bloat-rerun-NEW
python3 benchmarks/fixed_asgp/optimization/results/bloat-cost-20261014/summarize_final.py --runs logs/optimization/bloat-rerun-NEW --out logs/optimization/bloat-rerun-NEW/summary
```

Wrapper 拒絕覆寫輸出、不 prepare inputs、序列化 GPU 工作。短搜尋每次是含 cold start 的獨立 process；fixed-parent formal 另使用 3 processes × warmup + 3 measurements。不得把 fixed-parent 倍率替代跨代搜尋目標。

本輪四條路線已取得實測採用／淘汰證據。House30× 與 CPU 接近 ASGP 尚未完成；目前證據不支持繼續微調被淘汰的 CSE／workspace 原型。後續若要投入新的大型方向，應先證明低準備成本的 evaluator 特化能攤銷新 phase 的產生成本；不能靠後期程式縮小或改搜尋品質來冒充相同工作加速。
