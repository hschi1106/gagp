# 30 題執行修復與低預算效能結果（2026-10-15）

這輪修復讓 30 題都能完成 P1024／完整 1024 cases／4 代的原生 GPU-er-o 演化；**效能目標尚未全面達成**。20 題快於 ASGP 1T，10 題仍較慢。原本三題的長程結果約為 20.74×／19.23×／3.00×，不能宣稱全面回到歷史 e2e 水準。

## 版本、契約與計時

- 分支 `fix/asgp-all-task-runtime-20261015`，工作目錄 `gagp-support-20261015`；原始工作目錄、未提交修改、frozen inputs 與歷史結果未覆寫。本輪未 merge main／push。
- 採用的 runtime checkpoint 是 `8249c0d`。後續 transpose／Slice-hash 原型已撤除；`git diff 8249c0d..96152cb -- cpp` 為空。正式粗測使用 smallhash-screen 留存的該版 binary，最終 build 另驗證相同原始碼。完整 binary SHA、輸入 SHA、設定及命令見 [final manifest](final-all30/manifest.json)。
- 完整啟用設定見 [settings-final.json](settings-final.json)。包括原有 native 最佳設定，以及 `GAGP_GPU_ADAPTIVE_REPLAY=1`、`GAGP_GPU_PAYLOAD_WORKSPACE_MIB=8192`、`GAGP_GPU_SMALL_INT_HASH=1`；不能只開這三個開關就當成完整設定。
- ASGP 與 GAGP 均一個 application CPU thread；GPU-er-o 的 selection／variation 仍在 GPU，未加入 CPU fragment reproduction，也沒有改用 CPU 多執行緒。五種原生模式保留。
- 主表 P1024／C1024／G4／seed0，每題一個 cold process，**粗測，無精密重複或完整 scaling**。GPU30題加兩個同輪 ASGP controls 約 266.8 秒。ASGP 分母沿用同契約、相同 frozen 輸入的 `benchmark-all-g4`；本輪變動為 native 路徑，ASGP source/binary logic/settings 未變。同輪 House／Median ASGP 漂移 +0.58%／+2.46%，接近 1× 的結果不宜解讀為顯著優勢。
- 時間是既有 `evolve_wall_ms`：從未評分的 in-memory population 進入搜尋，包含初始 admission／GPU 整合、所有 generation、最終 population 評估與必要 native 結果 materialization。外部檔案讀取、grammar/adapter 準備、獨立 CPU 稽核及 JSON 寫出不在此 timer；`process_s` 另外保存，不能把表格當作完整 CLI 啟動到退出的比率。
- 未使用 Nsight profiler；保留同契約的內部計時及 case counters。診斷分項不從 search wall 直接相減，重疊項不相加當成精確分解。

## 全部題目的加速比

加速比 = 同題 ASGP 1T search wall / GAGP GPU-er-o search wall。小於 1 表示 GAGP 較慢。全部使用完整 1024 cases。

| 題目 | ASGP 1T (s) | GPU-er-o (s) | 加速比 |
|---|---:|---:|---:|
| Filter Odd | 10.973 | 16.081 | 0.68× |
| Sum Of Elements | 10.217 | 0.494 | 20.68× |
| Is Sorted | 11.585 | 10.444 | 1.11× |
| Index Of Element | 11.663 | 1.045 | 11.16× |
| All Equal | 11.094 | 9.777 | 1.13× |
| Cumulative Sum | 16.288 | 15.969 | 1.02× |
| Fuel Cost | 10.189 | 0.505 | 20.19× |
| Luhn | 4.083 | 0.417 | 9.79× |
| Min | 11.575 | 0.516 | 22.45× |
| MPI | 9.127 | 0.519 | 17.60× |
| Count Odds | 9.530 | 0.498 | 19.13× |
| Last Index Of Zero | 9.511 | 0.974 | 9.77× |
| Small Or Large | 0.385 | 0.867 | 0.44× |
| For Loop Index | 9.372 | 15.656 | 0.60× |
| Double Letters | 39.507 | 54.141 | 0.73× |
| Replace Space With Newline | 29.321 | 53.234 | 0.55× |
| Even Squares | 18.223 | 23.420 | 0.78× |
| Sum Of Squares | 5.626 | 0.413 | 13.63× |
| Negative To Zero | 15.878 | 17.023 | 0.93× |
| Scrabble Score | 16.048 | 0.549 | 29.22× |
| Grade | 0.355 | 0.866 | 0.41× |
| Median | 0.269 | 0.324 | 0.83× |
| Smallest | 0.274 | 0.323 | 0.85× |
| Syllables | 17.612 | 0.571 | 30.83× |
| Fibonacci | 4.066 | 0.645 | 6.31× |
| Tribonacci | 4.708 | 1.033 | 4.56× |
| House Robber | 4.252 | 0.443 | 9.60× |
| Subregion Sum | 1.286 | 0.434 | 2.96× |
| Fibonacci Array | 7.863 | 2.080 | 3.78× |
| Stock With Cooldown | 6.280 | 1.199 | 5.24× |

完整原始單次結果、最終 fitness、unique genomes、host RSS、profile 與稽核資訊： [speedups.csv](final-all30/speedups.csv)、[summary.json](final-all30/summary.json)、[逐代記錄](final-all30/generations.csv)。沒有把失敗／慢題隱藏，也沒有混入 P8 smoke 的加速比。

## 原本三題的長程比較

同一份本輪原生設定、各一個 cold process，沿用各自相同契約的長程 ASGP controls。這張表與 G4 主表的分母不同，不能交叉套用。

| 題目 | 代數 | ASGP 1T (s) | GPU-er-o (s) | 加速比 | 最終 native fitness |
|---|---:|---:|---:|---:|---:|
| Sum Of Elements | 32 | 49.261 | 2.375 | 20.74× | 0 |
| House Robber | 32 | 27.251 | 1.417 | 19.23× | -5470 |
| Median | 64 | 2.813 | 0.937 | 3.00× | -642 |

三題逐代 best 與完整 final population 的 fitness/nodes/wrong 記錄，均與本輪優化前的同契約長程結果一致；不是完整 AST 逐位元相同的證明。Sum 初始 population 已有解，不能用它證明搜尋能力。Median 現行 ASGP 控制組約 2.813 秒，歷史約 5.8 秒的分母屬於不同 fitness／搜尋契約，不能拿來宣稱現在仍有 6–7×。

## 修好了什麼、保留了什麼

1. 修正 list 邏輯配置額度與物理 scratch 容量的混淆；保留必要的邏輯 resource error，物理容量不足改為有界 GPU arena replay，不把它當作錯誤 fitness，也不轉 CPU 評估。
2. GPU list window 保留 backing storage／alias，避免失去 payload；私有索引改善 lookup。另修正 scalar region rollback 後 stale index 指向被覆寫 entry 的正確性問題，並測試 collision probe chain、survivor、重新註冊與生命週期。
3. CPU 讀取 invocation-owned list 不再反覆複製整份 payload。Negative to Zero 的 CPU1T P1024／G1 加最終評估完成於 70.754 秒，峰值 RSS 約 274 MiB、CPU audit 0 mismatch；先前 P8／95.36 GiB 的失敗仍保留。這不是 CPU 已追上 ASGP、也不是長跑記憶體上界的證明。
4. 補齊 source grammar 需要的可泛化 primitive／lowering：nonnegative index、integer character 的 ASCII letter 判斷、clamped list lookup、Stock state 的 demand-site lowering 與固定 skeleton constant-pool normalization。沒有依題名注入 solver；known witnesses 僅用於稽核，不加入演化 population。
5. exact fused Concat 合併驗證／複製／hash 工作；小整數查表只替代 `Int[-256,255]` 的既有 shallow FNV 結果，其他值走原函式，外層 hash、token、fuel、配置額度不變。固定 P1024 Filter evaluator 5554.75→4793.95 ms（約少 13.7%），P64 Filter／Double 約少 20%／19%。這是固定 workload evaluator A/B，不是單獨的 ASGP e2e 加速比。
6. 有界 tiled replay、依實際 overflow 原因增長容量，以及 8GiB workspace 設定降低重跑成本。這有明確記憶體代價：default 仍 512MiB；8GiB cap 包括 runtime workspace，但不含 immutable inputs／executable／CUDA context；曾觀察 device 約 9992MiB，並非嚴格 peak。不是免費的演算法加速。

相同 G4 契約下，Is Sorted 46.965→10.444 秒，All Equal 38.539→9.777 秒，Stock 2.382→1.199 秒。先前七個 GPU process timeout 的列表題本輪全數完成；不可將 timeout 門檻直接當成之前的搜尋時間。之前能完成的23題，22題完整 final population 指標及逐代 best 保持一致；Stock 因恢復既有 native GPU phase reproduction，軌跡不同，另有三 seed 短程／heldout／CPU 稽核資料。

## 目前成本來源

以下為 G4 同輪內部計時的逐代中位數，單位 ms；僅作診斷，不能把中位數或重疊項相加回推 search wall。

| 題目 | generation | evaluation | reproduction |
|---|---:|---:|---:|
| Filter Odd | 3449.872 | 3442.274 | 6.620 |
| Double Letters | 11929.322 | 11922.756 | 6.148 |
| Is Sorted | 1883.119 | 1877.412 | 5.568 |
| Small Or Large | 99.082 | 92.402 | 6.583 |
| Sum Of Elements | 50.379 | 43.521 | 7.028 |
| House Robber | 35.252 | 26.801 | 8.749 |
| Median | 12.419 | 4.592 | 7.747 |
| Stock With Cooldown | 172.679 | 159.312 | 12.260 |

Filter 的 initial kernel wall／replay event 中位數約 2027／1366 ms，Double 約 2610／9221 ms；長列表的 eval／hash／配置／replay 是主因，單純再省幾毫秒 reproduction 無法解決。Median 的 reproduction 約 7.75 ms，其中 kernel 約 6.18 ms、decode 約 1.18 ms；仍有不同於列表題的瓶頸。Small or Large／Grade 的 grammar 會建立 IntList 字元結果，不能靜默當成純 immutable label profile。

## 正確性、品質與公平性限制

- 已有全30題 known-witness gate、source/native 輸出抽查（每題前8個 source programs）、P1024 初始 membership admission，以及 CPU/GPU 完整案例 snapshot 證據。這不是對所有可演化程式的形式證明。
- 本輪 P1024G4 所記錄的 generation 評估總計 125,829,120 program/case pairs；timeout=0、evaluator fallback=0、unscored=0。runtime errors=11,323,055，為演化程式的受控錯誤／懲罰，不能說所有個體都無錯誤。GPU variation operator fallback 共103次，donor fallback=0；各次完整計時，未轉 CPU reproduction。每題 final top16 CPU 重評，共480程式、0 mismatch，並非全 P1024 offspring 的獨立重評。
- 最終五模式 smoke／全量 build 與測試結果另見 [validation.json](validation.json)。P8 smoke 不當成 P1024 速度證據。
- fitness 的 task metric／proximity／ceiling／tuple summary 參數與 elite count=1 已對齊；**搜尋契約仍非完全相同**。native physical depth24/nodes1024 與 ASGP source logical phase depth7 不同；balanced production weights 只是近似 grow 類別取樣。crossover gate／site distribution／donor retries、ranking 的40-bit canonical score與native nodes、RNG、N或N−1 offspring，以及 fuel/frame/memo/arena 預算仍有差異。不要將型別正確、grammar admission 與相同搜尋空間混稱。
- 原始 checked ASGP CumulativeSum list combine grammar 無法表達一般 prefix sum。這輪 build 使用明示的對稱 `list-combine-v1` extension：所有 IntList DC combine grammar 加入通用 head／last／singleton productions，source 與 native 同步。它不是完全未修改的 ASGP 論文重現；原始 source/default-OFF build 與 artifacts 保留，兩種結果不能混稱。
- cases 是目前 local task-domain 的1024案例，不是全套官方 PSB heldout。Fibonacci／FibonacciArray／Tribonacci 的 frozen input 本身僅一個 unique input，仍完整保留1024案例；不能宣稱有1024個獨立輸入。
- 本輪主要是 correctness／coarse performance。三 seed 短程與 heldout 僅排除明顯退化，不能證明收斂品質等價。各題 ASGP/native final fitness 保存在 CSV；不同演化 population 的後期時間不是 identical-workload speedup。

## 未採用與隔離的方向

已撤除：P1024 OOM 的 local arena、無明顯收益的 arena-window／compact-root、immutable-window hash cache、Levenshtein endpoint trim、實際 grammar 不適用的 readonly profile、索引 transpose、Slice／Singleton 的額外 hash 查表。測試隨已刪功能撤除，共用安全／正確性測試保留。原始結果與 binary 留作歷史證據，不再當作可啟用功能。

source-cap7／uniform-phase／0.7-crossover gate 是明示的 optional 搜尋規則實驗，**未加入主表設定**。cap7 有6個不支援 grammar 組合明確拒絕；House 三 seed 中兩個品質較差，不能為了報較高加速比直接替換主線。較早未把新 certification 納入計時的 cap 實驗僅作探索，正式後續已修正 timer。參見 [cap計時與品質](projected-long-timed/)、[uniform phase](uniform-phase-quality/)、[crossover gate](source-gate-quality/)。

Private offset-handle list executor 沒有實作：現有 packed-token collision／first-match lookup 即使沒有顯式 equality 也會影響 Index／Concat／fitness。直接跳過 intermediate hash 並不等價；它需要另外指定 execution contract、邊界驗證與 lifetime 證明，不能稱為幾行無損優化。

## 復現與未完成項目

從 repo root 執行，`--out` 必須是新目錄；使用留存的 runtime binary 與 frozen inputs，不重新 prepare：

```bash
PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/repair-20261015/scripts/gagp-final-all30.py --out logs/repair-rerun-NEW
PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/repair-20261015/scripts/gagp-final-three-long.py --out logs/repair-long-NEW
PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/repair-20261015/scripts/gagp-final-smoke.py --out logs/repair-smoke-NEW
```

完整 raw logs、逐代 JSON、exported programs 與 archived binaries 位於 `logs/all-task-repair-20261015/`。GPU 測試循序使用 `/tmp/gagp-support-20261015-gpu.lock`。目前尚未做到所有題目都有顯著 GPU 優勢，也沒有本輪全30題多 seed 長跑／完整 scaling／精密9次量測；這些刻意未執行，以符合低預算限制。

下一個真正高影響方向是長列表的可驗證執行表示／容量需求推導，以及 Median 的 GPU variation/lowering 成本。已測的小型 cache/layout/hash 延伸沒有支持繼續堆疊微調的證據；需要新的最小原型與品質／成本證據，不能把尚未完成的設計當成果。
