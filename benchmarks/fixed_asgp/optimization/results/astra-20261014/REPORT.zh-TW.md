# Astra 平行優化整合報告 — 2026-10-14

本輪已完成四個方向的實作／實驗及有效改動整合。**House 完整搜尋由 21.02× 提升到 26.00×，尚未達到 30×；Median 由 5.16× 提升到 6.45×。GAGP CPU 仍未接近 ASGP。** 三個 Astra-medium 子代理使用獨立 worktree；主代理負責取捨、整合、額外 import 重用及最終驗證。GPU 性能測試、build 與大型檢查共用鎖，依序執行。

本輪保留 CPU1T + GPU 與五種原生模式。沒有 CPU fragment backend、CPU reproduction 多執行緒、case sampling、題名 solver、fuel／精度縮減，沒有保留新的 RNG 或 variation 規則。預設設定不變，最佳組合仍須 opt-in。

**版本與重現。** runtime commit：`5a3be05`；branch：`opt/gpu-generation-20261001`；worktree：`/home/hschi1106/.t3/worktrees/gagp/gagp-opt-20261001`。原始 worktree 與 frozen inputs 未修改，沒有 push。最終二進位、完整 flags、source commit 與輸入 hashes 均記錄於 [final.json](final.json)，不是只記一個 commit。

在完整上一版 flags 上新增：

```text
GAGP_GPU_FUSED_DONOR_SITES=1
GAGP_CPU_FUSED_ADMISSION=1
GAGP_CPU_COMPACT_ADMISSION=1
GAGP_GPU_CONSTRUCTIVE_EXPORT=1
GAGP_REUSE_IMPORT_CERTIFICATE=1
GAGP_PREPARED_CASE_VIEWS=1
```

完整原始量測根目錄：`logs/optimization/astra-20261014/final/`；CPU／ASGP 三次控制組：`logs/optimization/astra-20261014/final-controls/`。重現最終 GPU、長跑、fixed-parent 與 scaling 檢查：

```bash
python3 benchmarks/fixed_asgp/optimization/results/astra-20261014/run_final.py --out logs/optimization/astra-rerun-NEW
```

這個 wrapper 沿用既有 runners，拒絕覆寫結果，不執行 prepare。CPU／ASGP search controls 使用同一套 `short_search.py --modes asgp_1t gagp_cpu --repeats 3`，傳入 final.json 全部 GAGP flags，另選新的輸出目錄。最終 CPU controls 的 archived binary、CPU 實作與相關設定未變，予以沿用；後續只改 GPU view preparation，因此 GPU-e 重新量測。Scaling 的全部 controls 都重跑。

**主要結果：P=1024、完整 1024 cases。** House 使用 seed0、32 代；Median 使用 seed2、64 代。完整搜尋包含冷啟動、admission/import、每代所有工作、最終完整評估及必要 AST 匯出。額外 audit／JSON 序列化／輸入解析另列 process wall。正式量測沒有開 perf、nsys 或 NCU。

| Mode | House 32 generations | Median 64 generations | Processes |
|---|---:|---:|---:|
| ASGP 1T | 27.576167 s | 5.899860 s | 3 |
| GAGP CPU | 82.103390 s | 42.153159 s | 3 |
| GPU-e | 8.398843 s | 24.131383 s | 3 |
| GPU-er | 1.063300 s | 0.915630 s | 3 |
| GPU-er-o | 1.064777 s | 0.909164 s | 9 |

Headline 分母固定為既有 ASGP search baseline：House 27.687368225 s、Median 5.864915193 s；不替换為本輪較有利的控制數字。GPU-er-o 本輪同時段 ASGP 對照則約 25.90×／6.49×。GPU-er 與 GPU-er-o 的細小差異不能解讀為 overlap 的獨立收益；這個既有 owner profile 的主要依賴流程相同。

GPU-er-o 相對上一版：House 1.317052 → 1.064777 s，時間下降 **19.15%**；Median 1.136319 → 0.909164 s，下降 **19.99%**。House 的 30× 門檻是 922.912 ms，還需減少約 **141.9 ms／13.3%**。

九次 fresh-process 原始完整搜尋時間，單位 ms（冷啟動均保留）：

- house_robber: 1064.254311, 1062.523316, 1069.721830, 1059.246598, 1064.776693, 1071.684982, 1060.895525, 1066.094354, 1065.030011。範圍 1059.247–1071.685。
- median: 914.770751, 908.363092, 908.622689, 908.892544, 911.705759, 911.566284, 908.159194, 909.163542, 913.916061。範圍 908.159–914.771。

所有模式的 raw repeats／process 時間在 [final.json](final.json)，每代時間、fitness、大小與 operator/error counters 在 [generation-times.csv](generation-times.csv)。GPU-er-o 完整 process wall 中位數是 House **2.295 s**、Median **2.070 s**；以本輪 ASGP process wall 比較約 **12.20×／3.12×**。因此 **26.0×／6.45× 是既定搜尋邊界的倍率，不是包含整份 audit／JSON 輸出的 CLI process 倍率**。

**哪些改動留下，實際消除了甚麼。**

| 方向 | 決策與證據 |
|---|---|
| Static typed phase | GPU superinstruction、power-of-two division、精確 guarded int32 division，以及 CPU cached fusion 均淘汰。沒有可靠的整體淨收益；一次 CPU apparent win 是 OFF 路徑退化，對照 archived binary 後排除。runtime／flags 已移除，僅留證據與 patch 紀錄。 |
| Native grammar admission | 保留 unique-head matching 與 witness construction 合併；保留受控 scalar AST compaction 後延續已完成的 admission／lowering。不同名字表或非適用 payload 使用原路徑。單獨配對 GPU-e 實驗約改善 15.0%／11.0%。 |
| GPU donor／metadata | 保留產生 donor 當下寫入 construction sites，取消第二次 annotation traversal；原 RNG、sites 與 offspring 完全相同。Mutation local frame 4784→1712 bytes；單獨配對完整搜尋改善 1.39%／3.08%。 |
| Bloat／search rules | size-near crossover 淘汰並移除。House 的品質退化在相同時間與獨立 cases 下仍存在，不採用較小程式帶來的表面速度。 |
| Import／export 延伸 | Import 核對既有 private certificate 後重用 witness／bytecode；失配照常驗證／編譯。私有 GPU owner 的 export 不再僅為附上 optional cache 而反向重建 canonical witness。外部 replay 仍嚴格 admission。Export 單獨配對完整搜尋各改善約 10.8%，process wall 也改善。 |
| Prepared case views 延伸 | 對已證明支援 view、且 region 有 IntList parameter 的程式，每次 evaluation 建立當前 payload table 的 private descriptors，避免每次 phase lookup。沒有降低上限。單獨交錯 A/B：House −1.67%，Median +1.16%；因此不能宣稱此步全面加速。 |

各方向的百分比不可直接相加。Import 是重用既有 immutable executable/cache，不是移走必要驗證；export 是建構合法性取代重複 canonical reconstruction，不是把 type correctness 冒充 grammar membership。Prepared views 的重建、額外 allocation／upload 都仍計時；它把反覆 device lookup 改成每次 eval 的 host preprocessing，並非零成本。

保留的流程沒有改數值、fuel、operator 或 RNG。唯一明確的 admission 契約差異是 `GAGP_CPU_FUSED_ADMISSION` 的 speculative matcher-work guard 計數方式；grammar language 與 strict membership API 不變，極端 work-limit 邊界不宣稱完全相同，已同步 spec 與測試。Scalar compaction 先驗證原輸入，不能藉刪 unused entry 接受原本不合法的資料。

最快路徑仍沿用 native GPU phase profile 的既有支援範圍：固定 root 結構、可獨立的 phase holes、受支援 nonterminal／identity scope 與容量條件；不是宣稱所有 grammar 都有同一倍率。Repeated holes、複雜 lexical/template 組合等不適用條件仍明確 fallback。沒有擴大有限 phase bank 或換成 CPU reproduction。

**最新成本。** 以下為同一最終未開 profiler 系列中的內建計時中位數，單位 ms。Kernel、pack、upload 是 eval／repro 的子項，不能再與外層重複相加；各列中位數也不保證精確相加為總中位數。

| GPU-er-o cost over entire search | House32 | Median64 |
|---|---:|---:|
| Initial admission + native import | 101.079 | 94.689 |
| All generations | 781.456 | 654.758 |
| Evaluation calls | 632.983 | 244.954 |
| Eval kernels (nested) | 577.976 | 157.603 |
| Reproduction calls | 146.062 | 404.016 |
| Reproduction kernels (nested) | 104.231 | 301.889 |
| Bytecode integration (nested) | 26.431 | 64.134 |
| Eval packing (nested) | 15.581 | 25.200 |
| Eval upload (nested) | 28.308 | 46.431 |
| Final evaluation + AST export | 37.647 | 14.260 |

House 剩餘主因是 eval；Median 則是 GPU reproduction／lowering。GPU fitness-session 的 `gpu_init_ms` 約 2 ms，**不包含更早的 reproduction device preflight**。總時間尚有約 140 ms 的外層 setup／cleanup 等成本，沒有把它漏掉，也沒有用這個差額假稱已精確量到某一個 CUDA API。

CPU 的逐代 evaluation 總和仍約 House **72.52 s**、Median **21.00 s**；reproduction 約 **6.36 s／20.46 s**。即使只消除 admission，也不足以接近 ASGP。這些跨架構 population 不同，不能把全部時間差當成同一工作量上的 interpreter 速度差。早期 perf 證據只用於選方向，不與最終端到端時間混算。

**正確性、品質與長跑。** 最終 127 native tests 通過；24 repository checks、89 tool tests 通過（4 個既有 skip）。兩個 focused CUDA memcheck 都是 0 errors。Spec hash check 曾指出未更新的 manifest；已同步 hash／契約說明，沒有刪測試。

36 組最終 short／128-generation traces、完整 final ASTs 與 history ASTs 和各自參考完全一致。Full cases、error／timeout／fallback、operator、fitness／大小都比對，不只是最終最好分數。固定 1024 programs × 1024 cases 的獨立 admission/CPU/GPU snapshot before/after 完全一致；House 原有 CPU/GPU 差異仍在 indices347/359/622/766，Median 無差異，**不宣稱全域 CPU/GPU parity 已修好**。

GPU-er-o 三個 seeds ×128 generations 最終 fitness：House [-4427,-865,-4535]，Median [-478,-642,-642]。House seed1 曾達 -768，最終退至 -865；沒有隱藏非 elitist 演化的回退。短程／128 代 replay 只能排除本輪引入的退化，不證明與 ASGP 收斂品質相等。既有 House32 的 ASGP / CPU / GPU-repro 最終 fitness 分別約 -4234 / -6544 / -7146；Median64 約 -578 / -642 / -642，跨架構搜尋品質仍有差異。

長跑 bloat 仍在：House seed0 前16代單代中位數 **21.23 ms**，最後16代 **174.77 ms**；Median seed0 **11.91→30.16 ms**。Final unique genotypes 沒有因新實作下降；所有長跑程序／案例完整計算，沒有新增 timeout／fallback。Median 存在與原軌跡相同的 runtime errors，不能寫成零錯誤。

128-generation process peak RSS 約 **1.16–2.28 GiB**，包含 history、AST 和 JSON 輸出；genotype/device transport high-water 約 **0.54–0.74 GiB**，不等於全部 CUDA memory。Owner 沒有新增無界祖先／cache 保留，但這些 peak 數字不是無限長跑或 host memory 恆定的證明。詳細紀錄：[validation.json](validation.json)。

被淘汰的 bloat 模式做了 13 組可覆蓋的相同時間 paired comparisons，另5組因128代 trace沒有涵蓋時間預算而排除；每組都在 train 選 champion 後獨立檢查完整1024 heldout cases。House 在2秒下三個 seed 的 train／heldout 都更差。這是淘汰依據，不是把 heldout 當搜尋資料。[數據](rejected-bloat-heldout.json)。

**Scaling 是另一個實驗。** 下表是 fixed-parent、完整單代，使用本輪每個 population 自己的 ASGP controls；不可代替上面的完整搜尋倍率。40／40 格五模式完成，沒有 OOM；完整各模式時間在 final.json。

| Population | House GPU-er-o | House ratio | Median GPU-er-o | Median ratio |
|---|---:|---:|---:|---:|
| 1024 | 32.779 ms | 40.15× | 6.999 ms | 8.42× |
| 2048 | 50.964 ms | 51.17× | 14.558 ms | 8.00× |
| 4096 | 99.559 ms | 52.65× | 24.973 ms | 9.11× |
| 8192 | 197.847 ms | 52.93× | 51.005 ms | 9.25× |

另做三個 processes、每個暖機一次＋三次正式樣本的 fixed-parent9：House32.229 ms、Median6.956 ms。Sum 的五模式P1024回歸也完成；其同輪 ASGP／GPU-er-o 單代約1901.491／46.378 ms。這些都不是 House32 代的30×達標證據。

**收尾判斷。** 保留六個有獨立驗證的 opt-ins，撤除沒有淨收益／品質變差的 runtime 原型。這輪四個方向與必要延伸已實測，沒有證據支持繼續擴寫被淘汰的 static／bloat 原型。House30×與CPU接近ASGP仍未完成；沒有將其改寫成單代已達標。後續若繼續，應先為不同 executor 或更便宜、可證明的 construction/admission 提供新的成本證據，而非重試同一批小改動。恢復入口與目前最佳設定在 [PROGRESS.md](../../PROGRESS.md)。
