# 可自訂 grammar 與 GPU 優化驗證報告

**最新驗收狀態：使用者於 2026-09-27 明確接受目前結果，將 Goal 11／11.5
標記為達成。** 原始 Q≥0.95 數值門檻仍未達到；以下數據與歷史判斷保留，
不改寫為數值達標。Goal 12 的最終整合與交付驗收仍為獨立工作。

Goal 12 後續範圍已由使用者擴充為：完整 repo 盤點與清理、移除確認不再需要
的檔案、整理程式碼模組及文件架構、完善新手安裝與 CPU/GPU 操作指引、
自訂 grammar 到輸出／replay 的完整教學，以及乾淨 checkout 的整合驗證。
保留必要基準與可追溯證據，歷史資料與目前使用說明分開；不以刪除使用者
檔案或必要測試換取整潔。此為新增待執行範圍，並非宣告整理已完成。


2026-09-27 最新續跑實作：`36c8cea692f4e82fdb8612d0a8480ce16f8d056b`。
平行重建 child AST／metadata，batch 256、最多 20 workers；完整驗證與搜尋
規則不變。13 種探索設定後保留此項，配對 GPU generation 中位數改善 5.26%。
相同最終 binary 在兩組測量的 CPU/GPU 加速比為 **31.4393／30.4398 倍**；
六次觀測合併的描述性比值 **30.7458 倍**，direct 為 **25.9176 倍**。
未穩定達到估計的 32–35 倍，原始 Q≥0.95 仍未通過。使用者已要求沿此
方向繼續到局部優化接近極限；本輪依此停止判斷收尾，沒有待回覆的 overlap
澄清。完整數字、回退試驗、正確性邊界與證據見
[第二輪優化報告](grammar-migration/goal-11.5-target35.md)。

以下保留**第一輪**實作 `c574b7b0ef609b950f71c7f02336f6fad37adeff` 的報告；
其中完整六工作量矩陣與廣泛測試屬於該版本，不改標為最新版本數據。
本報告區分「這次 p1024 優化結果」、「原始 Goal 11 門檻」與完整功能驗證；
不能把其中一項通過當成全部通過。更早的實驗、失敗與回退保留在
[續跑紀錄](grammar-migration/goal-11.5-resume.md)。

## 第一輪結果

原先凍結的 `simple_exp_1024-p1024`，完整 1024 成員、1024 cases，
一個完整 generation，以 CPU／GPU 時間中位數相除：
**新版 overlap 30.2773 倍；新版 direct 25.5780 倍**。
超過 30 倍需要啟用 `--repro-overlap on`，並在 CPU/GPU 都使用同一份
`tcmalloc`。這不是純 GPU kernel 的加速比，也不是把 preparation 移出計時。
三個 CPU／overlap 配對各自均超過 30 倍，但樣本只有三次，不能保證所有後續
執行、不同硬體或其他工作量都超過 30 倍。

同 allocator 的舊版 direct 是 37.8023 倍，跨模式對照 Q=0.8009；
舊版也開 overlap 時是 45.959 倍，同模式 Q=0.6588；
若對照歷史預設 allocator 的 41.9585 倍，Q=0.7216。
原始 Q≥0.95 與 direct >30 倍均未達成，不宣告原始 Goal 11 完成。
第一輪保留 overlap，兩種模式的結果都完整保留；後續使用者要求沿此方向
繼續優化，最新狀態見上方第二輪報告。

## 相同工作量的比較

單位 ms；每列為一次暖機、三次測量的中位數。舊版同 allocator 數據沿用
緊接之前的同機實驗，並非與最終新版逐次交錯測量。Stage A 與歷史基準
是既有預設 allocator 證據，不與 tcmalloc 數字混為同一對實驗。

| 版本與設定 | CPU generation | GPU generation | CPU/GPU |
| --- | ---: | ---: | ---: |
| 歷史原始基準、預設 allocator、direct | 3113.729 | 74.210 | 41.9585x |
| Stage A、預設 allocator、direct | 6044.358 | 438.732 | 13.7769x |
| worker/window checkpoint、預設 allocator、direct | 6034.888 | 279.175 | 21.6168x |
| 原始基準、tcmalloc、direct | 2660.746 | 70.386 | 37.8023x |
| 原始基準、tcmalloc、overlap | 2660.746 | 57.893 | 45.959x |
| 最終新版、tcmalloc、direct | 5245.560 | 205.081 | 25.5780x |
| 最終新版、tcmalloc、overlap | 5245.560 | 173.250 | **30.2773x** |

最終三次 CPU 是 5385.446／5245.560／5213.354；direct 是
206.070／205.081／202.150；overlap 是 177.498／169.709／173.250。
沒有刪除已完成的慢樣本，也不以最快的一次作驗收。

同 allocator 下，新版 CPU 耗時仍為原始基準的 1.971 倍；新版 direct GPU
為 2.914 倍，overlap GPU 為舊版 direct 的 2.461 倍。
因此「加速比突破 30 倍」不代表新版絕對時間已追上舊版。
與 Stage A 的歷史 GPU 耗時相比，最終選項快約 2.53 倍，包含程式、allocator
與 overlap 設定一起改善的效果，不能全歸因於單一 patch。

新舊皆保留 fuel 20000、blocksize 1024、完整 frozen population、cases、seed、
操作比例、donor proposal/retry 數。原始 source budget 80 節點／深度 7
保留為投影限制，新表示法使用經驗證的實體上限 531／141；不是放寬原始
搜尋預算。generation 包含該代 preparation、evaluation、reproduction 與
必要 cleanup；額外的 final evaluation 不混入一代計時。

六組代表性工作量、四種完整 generation 模式、CPU/GPU steady evaluation、
p64 anchor 五代演化全部已測量：296 筆 receipts，其中 276 個新程序、20 筆
已完成證據經核對後沿用。36 組同 backend 的 fixed-work fitness vectors
與基準完全相同。[完整時間／加速比／Q／cold CLI 表](grammar-migration/goal-11.5-final-measurements.md)
與 [machine-readable summary](grammar-migration/goal-11.5-final-measurements.json)
提供所有 40 個比較列及原始三次樣本。

主要限制不能忽略：p1024 若只把 evaluation 放 GPU、reproduction 留 CPU，
新版完整 generation 仍需 2557.767 ms，加速只有 2.051 倍；CPU grammar
reproduction 是重大成本。相同 frozen population 的 steady evaluation 卻有
102.803 倍（舊版 99.900 倍），顯示主要落差在演化／準備成本，不能用
steady 的 >100 倍替代完整 generation 的 30.277 倍。其他小族群／typed
payload 場景也有顯著回退；本次未達成所有模式的 Q≥0.95。五代 p64 的
GPU reproduction direct 為 10.119 倍、Q=0.9957，overlap 為 10.685 倍、
Q=0.9118；這不能推論所有 p1024 長代數都有同樣速度。
歷史選樣見 [代表性選樣](grammar-migration/goal-11-representative-selection.json)。

## 保留的實作與選項

| 修改 | 作用與邊界 |
| --- | --- |
| 批次 worker 重用、donor 排程窗口 | 每次 operation 擁有 worker team，跨批次重用；donor 窗口最多 1024 jobs、1,048,576 節點，保持原始順序、seeds 與 retries |
| evaluation bytecode 批次準備 | 在 generation 範圍內平行處理獨立成員，維持相同 bytecode 與 fitness 工作 |
| owned overlap | evaluator 與 preparation 共享不可變的完整 population；fitness 完成後才進行 selection；包含不用的 constants 在內都追蹤 payload 讀取，變動時重新準備 |
| resource proof 平行計算 | 獨立鎖保護 resource proof，在相同 generation 中與其他準備重疊，使用前 join；沒有 subtree mutation 時不做多餘 proof |
| donor analysis 與 parent root handoff | 只在私有、同來源與同 context 的流程重用分析；驗證 sealed payload snapshot，失效就走原本完整路徑；child 仍完整驗證 |
| scalar admission identity | 私有且有界的 donor admission cache 對 ≤256 bytes 的純 scalar native key 避免重複編碼；payload 類型仍用完整 decoded identity；不是 public cache 格式變更 |
| device arena 合併配置 | 同一塊對齊 GPU allocation 容納原本所有 arrays；容量、傳輸內容與 kernel 工作量不減少 |
| donor identity 平行驗證 | 私有 packing、至少 128 donors 時，最多 20 workers、每批 16；完整計算每個 donor identity、join 後依原序處理錯誤；public 與 active payload scope 路徑維持序列處理 |
| tcmalloc | 執行時可選，同一 library 同時套在 CPU、GPU 與比較基準；未加入必備建置依賴或修改系統預設 allocator |

新增 donor 平行驗證的 paired GPU overlap 中位數從 176.147 降到 173.250 ms，
改善約 1.64%；CPU 中位數的 0.55% 變動是測量差異，這個 patch 不改 CPU
演化路徑。額外 scratch 是每 donor 一 byte 與一個 exception pointer，加上
最多 20 份 worker-local identity 字串。其他快取有固定上限，payload snapshot
與未完成 preparation 的生命週期受 operation／generation ownership 控制。

已測但不保留的方向包括：Stage B 規則限制、metadata move/reserve 原型、
atomic executable flags、payload reader 分鎖、LTO、低階 SHA API、pinned host
allocation 合併、較少 donor workers、較小 blocksize、特殊 tcmalloc cache
設定，以及 jemalloc／mimalloc 替代組合。部分有單次小幅收益，但沒有優於
保留組合；個別時間、版本與回退原因在續跑紀錄，不把探索樣本當正式比較。
完整 incremental native certification 尚未實作；不能宣稱每個可能方向都已
窮盡。此次實際採用的是具有明確 ownership 邊界的分析重用。

## 規則與語意差異

**這次效能優化沒有留下新的文法或語意限縮。**
canonical binder IDs、縮短 local names、合併 stage-disjoint productions 的
實驗都已回退；Stage B 也已在保存證據後安全回退，保留 recovery branch
`archive/goal11-stage-b-20260927` 與外部 bundle／log。

先前已經由使用者接受的遷移差異仍存在：

- 交換相容性改用實際名稱與型別，不再以舊版名稱表的數字索引判定。既有
  64-member 比較的 1,648 個候選位置一致，但 186 個配對判定不同。這不是
  本輪為了速度新增的限制，也不保證舊／新版演化軌跡相同。
- 搜尋預算以 source 80／7 投影限制，實體 AST 可以較大；fuel 與 runtime
  bounds 各自維持明確規則。表示法膨脹不等於增加合法搜尋空間。
- v1 config 的 `constrained-intent-v1` 遷移是明確有損的設定轉換；舊設定
  缺少 constant domains、fuel、case schema 與固定 typed assignment environment。
  不能宣稱其完整隨機搜尋分布與 release 1 相同。
- materialized legacy AST／完整 member 的行為遷移與「用舊 seed 在新 generator
  重生」不同；後者不提供跨版本相同程式的保證。

本輪獨立驗證完整 ordered native AST records：1024 個 final members 與前一
window checkpoint 完全相同；另驗證 native membership、lowering 與 budget。
這證明本輪加速沒有削減工作或改變該 frozen run 的 offspring，不構成任意
未知 grammar 的形式證明。既有 CPU/GPU 邊界差異保留於
[語意證據](grammar-migration/semantic-coverage.md)，不靠改 golden 結果掩蓋。

## 架構與實作提交

資料流是 typed grammar definition／可重用 package → immutable CompiledGrammar
→ typed derivation／variation → prefix AST → 通用 lowering → CPU/CUDA runtime。
`cpp/src/evolution/grammar/` 負責定義、生成、membership、variation；
`cpp/src/evolution/repro/` 負責 host preparation／transport／GPU reproduction；
`cpp/src/runtime/` 負責執行與 payload。規範以 `spec/` 為準。

| Goal | 提交 | 結果 |
| --- | --- | --- |
| 01 | `3799536` | 凍結 reference、行為 oracle、比較工具 |
| 02 | `193ccb6` | typed grammar definitions／compiled tables |
| 03 | `51da014` | grammar generation、確定性 artifacts |
| 04 | `a1656f8` | typed crossover／mutation contracts |
| 05 | `d61fcf9` | lexical region、一般 sequence traversal |
| 06 | `2a52d65` | bounded recursive region、memoized state |
| 07 | `302b7b1` | 一般化 GPU execution／reproduction |
| 08 | `ddf58c4` | 資料定義 compatibility packages |
| 09 | `e0d8926` | 移除 specialized production core、切換 v2 |
| 10 | `6fe14ca` | authoring、migration、CLI、文件 |
| 11／11.5 | `2d11d4a`、`6538358`、`9cdc72e`、`7495153`、`a2630eb`、`d21b062`、`6337d36`、`c574b7b` | Stage A、Stage B 回退、效能修復與診斷修復；Q 原門檻未達 |
| 12 | 本報告與 clean-checkout 證據 | 功能／流程／代表性測量完成；overlap 是否作驗收模式待確認，不標示全 goals 完成 |

LinearRec package 是 lexical bindings 加 reverse traversal；DC 是有界的
sequence-window region 與有序 left/right requests；DP1D／DP2D 是資料定義的
coordinate/dependency plans 與 memo。核心不根據 ASGP/RSGP package 名稱派送。
舊 AST kinds 53–70 與 opcode 25–27 已保留不用，沒有第二套 production
legacy interpreter。詳見 [cutover](grammar-migration/goal-09-cutover.md) 與
[package 對應](grammar-migration/goal-08-compatibility-packages.md)。

## 遷移與使用者流程

| 輸入／行為 | 支援範圍 |
| --- | --- |
| 六個維護中的 v1 presets | `constrained-intent-v1` 決定性轉換，有損搜尋意圖映射 |
| materialized legacy AST／完整 generated v1 member | offline 轉為 `grammar-materialized-v2`，附 lossless constants／payload |
| v1 population | 先拆出完整 members，各別遷移 |
| legacy bytecode | 不直接轉換；必須取得 source AST |
| seed-only legacy artifact | 先用 frozen legacy build materialize；不能直接在 v2 精確重生 |
| v2 generation／population replay | grammar identity、request、case schema、seed 一致時確定性重播；檔案搬移不改 resolved identity |
| 公開型別 | Int／Float／Bool／Char／String／IntList／FloatList／StringList；無公開 generic list、closure 或任意 executable plugin |
| `--eval-ast-json` | 目前只支援 CPU；GPU evaluation 使用支援的 population／evolution 流程 |

乾淨 checkout 工作流程執行 59 個支援命令：安裝 Python wrapper、四個 authoring
examples 的 init/validate/inspect/resolve/generate/replay、CPU 匯出求值，
每例五種 CPU/GPU evaluation/reproduction 組合、scalar signature/weight 修改、
package 改名保持 resolved bytes、combine 規則修改、v1 config 遷移，以及 README
GPU 預設範例。另保存一次 GPU `--eval-ast-json` 的預期不支援失敗，沒有把它
算成成功命令。原生 binaries 是全新外部 Debug build，以 `cpp/build` symlink
指向該 build，沒有依賴舊 binary 或 private benchmark input。

簡單自訂例：先建立 scalar，再把 sum 的 `add(Int,Int)->Int` 改成
`mul(Int,Int)->Int`，其 weight 改為 3、input weight 改為 1；不需重編 C++。

```bash
.venv-tools/bin/gagp-tools grammar init --example scalar --out /tmp/my-grammar.json
.venv-tools/bin/gagp-tools grammar validate --grammar-definition /tmp/my-grammar.json
cpp/build/gagp_evolve_cli --grammar-definition /tmp/my-grammar.json \
  --cases configs/grammar/examples/authoring/scalar.cases.json \
  --engine gpu --repro-backend gpu --repro-overlap on \
  --population-size 16 --generations 2 --out-json /tmp/my-run.json
```

完整教學與限制見 [grammar authoring](../guides/grammar-authoring.md)。

## 正確性驗證與六項需求

| 驗證 | 實際結果 |
| --- | --- |
| fresh Debug/CUDA 全 native suite | `d21b062`：117/118；唯一失敗為診斷工具漏接新 overlap，`6337d36` 修復後該測試通過，118 個 distinct tests 全覆蓋；不是宣稱完整重跑 118 項 |
| 最終 donor parallel patch | Release 四項 preparation／transport／backend／evolution parity 全通過；fresh Debug 的 preparation／evolution parity 再確認通過 |
| Python tools | local 80 pass、4 skip；fresh remote 補跑 4 項通過，合計 84 distinct tests |
| Repository checks | 23/23；文件更新後最終連結／authority 檢查亦通過 |
| CPU-only | fresh Release build、evolution pipeline／donor tests 通過；最後 donor parallel patch 的 CPU-only preparation 測試亦通過 |
| frozen p1024 final population | 1024/1024 native membership/lowering 與完整 ordered AST comparison 通過 |

完整 native suite 實際執行 19 個 GPU-labeled tests，沒有把缺 GPU 當成通過。
LinearRec／DC／DP1D／DP2D 的 scope、variation、八種結果型別、error order、
fuel threshold、bounds、frame/memo/payload 容量由 package、lexical/bounded、
migration oracle 與 CPU/GPU tests 共同覆蓋。72 種 typed package generation
matrix、legacy differential 與限制的來源詳見 package／semantic 證據文件；
這不等同重製研究論文演算法或保證舊／新 RNG population 相同。

| 六項需求 | 狀態與依據 |
| --- | --- |
| 一般可自訂 grammar | 通過：schema、packages、四種 authoring 與自訂流程 |
| 移除 specialized core，保留支援的 migrated behavior | 通過其明列範圍：core guards、offline routes、oracle；有損 config／既有差異已揭露 |
| 所有 GPU flow 的代表性效能驗證 | 測量覆蓋通過：六組 workload、四模式、steady、五代；Q 門檻多數未達，不能宣告效能全面保持 |
| 針對回退做優化 | 已實作並測量；保留有益選項，原始 Q≥0.95 仍未達 |
| 完整可用 repository | 支援流程與 native/tool checks 通過；上述 public CLI 限制明列 |
| 最終可追溯報告 | 第一輪報告、完整測量表與 raw hash evidence 已提供；最新續跑見第二輪報告 |

## 硬體、重現與證據保存

Snoopy：Intel i9-10900K（10 cores／20 logical CPUs）、RTX 3090 GPU 0；
GCC 11.4、CUDA 12.6.85、driver 560.35.05。Release CUDA sm86，
`--maxrregcount=64` 與 frozen reference 相同，blocksize 1024。
`GAGP_CUDA_DEVICE=0`；本機被佔用的 GPU 沒有用於正式測量。
所有正式 CPU/GPU 比較套用同一個 `LD_PRELOAD` allocator；沒有刻意放慢 CPU。

- 最終 benchmark SHA-256：`75f53eaf85a69c24e959fa5b5de5f2d7f56e908330e1abcf32f9f06471e4281f`。
- tcmalloc SHA-256：`572af05b75e2366a3e8c06c29d3a04c0a538c9635c9957f2808846730ab81da5`。
- final population output SHA-256：`f401ece00609aca0c46c45ea4577ac66dcce3de37cd67fc3d5820dfa2a1c0525`。
- local raw root：`/home/hschi1106/gagp-artifacts/grammar-migration/target30-20260927`。
- remote raw root：`/home/hschi1106/gagp-resume-a-20260927`。
- clean audit root：local external `grammar-migration/goal12-audit-20260927`，remote `/home/hschi1106/gagp-goal12-audit-20260927`。

`target30-parallel-donor-summary.json` 包含每次時間、receipts 與驗證；
`parallel-donor-paired.py`／`parallel-donor-population.py`／`audit-final.py`
保存原始完整命令與 hash 驗證。`final-cpp-hashes.json` 記錄 363 個 source paths，
最終 Release 與 clean Debug 來源都逐檔一致；`final-code.patch` 保存從完整 suite
版本到最終 code 的差異。重現時使用上述 script 的相同輸入、旗標、binary 與
allocator，改用新的 output directory；不要覆寫既有成功 receipts。

證據中包含較早速度較好或較差的候選版本，僅上列最終 binary 的數字用作
本次結果。cold CLI 包含初始化與程序成本，steady evaluation 不含 generation
reproduction；這兩者都不能替代上面的完整 generation 數字。

最終 archive 是 `target30-final-evidence-v2.tar.gz`，SHA-256
`9f5c2f85ba3b6449b03fcad1a7fd69778126c2893aee95ef3a6abb2fbf377a81`；
下載解壓後 1,238 個檔案逐一核對全部一致。原始資料與 recovery evidence 已
保存在本機與遠端，沒有覆寫已完成的測量。
