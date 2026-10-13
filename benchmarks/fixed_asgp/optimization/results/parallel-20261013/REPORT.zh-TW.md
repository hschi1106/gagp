# 平行 worktree 優化結果（2026-10-13）

## 最終整合版 `2d7dac3`

最佳設定包含 `GAGP_CPU_ALL_FIT_PAIRS=1`，完整設定／binary SHA／input hashes在 [latest/final.json](latest/final.json)。五模式三個獨立process，GPU-er-o再加六個、共九次；原始 [repeats](latest/search-repeats.csv)、[每代資料](latest/generations.csv)。House32/seed0、Median64/seed2、P1024/C1024，CPU1T。

|模式|House搜尋秒|ASGP/本模式|Median搜尋秒|ASGP/本模式|
|---|---:|---:|---:|---:|
|asgp_1t|27.687|1.000×|5.865|1.000×|
|gagp_cpu|84.247|0.329×|44.762|0.131×|
|gpu_eval|9.698|2.855×|26.911|0.218×|
|gpu_repro|1.353|20.469×|1.166|5.031×|
|gpu_overlap|1.317|21.022×|1.136|5.161×|

House30×仍未達：本輪分母推導的門檻為922.912ms，尚需把1317.052ms降低約30%。Median5×門檻1172.983ms，九次皆通過，但最慢1169.326ms的餘裕很小，不保證未來每次都超過5×。CPU仍慢於ASGP3.04×／7.63×。搜尋計時包含初始化、最後evaluation與AST匯出／admission；完整process另外包含輸入解析、JSON輸出與獨立稽核。

完整process的GPU-o中位數House2.599s／Median2.354s，相對ASGP為10.822×／2.722×；不可與搜尋比值混稱。

既有native phase-owner profile沒有host donor preparation，GPU-er與-o共用相同dependency schedule（[契約](../../../../../docs/design/gpu-reproduction.md#experimental-native-gpu-phase-population)）。兩欄不同樣本數與環境下的時間差，不能解讀成overlap收益；本輪未改此既有排程。

新版本30個主run的final AST export全部與已稽核checkpoint一致。CPU all-fit無額外RNG／搜尋語意改動；GPU runtime沒有改動。此輪GPU時間稍低，同時其他使用者GPU工作自行結束釋放顯存，不能把變快歸因於CPU-only patch。其配對收益另見最後一節。

固定單代九次中位House32.926999ms／Median7.277462ms，歷史frozen分母下38.935×／8.489×。[原始repeats](latest/fixed-repeats.csv)。此比值不能替代跨代搜尋。

以最終binary重現：使用本文末的完整命令；摘要重建加 `--followup`。原始b60a336 checkpoint保持不變。

最終scaling **40/40格全部成功**，兩題×P1024/2048/4096/8192×五模式，控制組皆重跑；[原始scaling](latest/scaling.csv)。其他使用者工作自行釋放顯存後，Median8192也成功，先前OOM紀錄仍保留。Sum五模式smoke成功。[最後整合驗證](latest/validation.json)；[30個run逐代非時間欄位／finalfitness與size完全一致](latest/exact-traces.json)。

最新整合版stage timer：house_robber CPU末8代的跨process中位：eval 2523.7ms、repro 357.3ms；median CPU末8代的跨process中位：eval 320.1ms、repro 334.3ms。這些診斷支持House仍以CPU evaluator為主、Median兩段都昂貴；不是從profiled時間扣除正式總時間。

## 以下保留 b60a336 checkpoint 的完整分析與驗證

本表先保留已驗證的 `b60a336` checkpoint；最終CPU all-fit整合版 `2d7dac3` 另存 latest/，不覆蓋這批歷史原始結果。

## 結果與計時契約

保留五種原生模式、CPU1T、P1024、完整1024 cases。GPU-er／-o 的 selection、crossover、mutation 仍在GPU；沒有恢复CPU fragment。主要量測是真正跨代的House32/seed0、Median64/seed2。

- House GPU-er-o **1349.259ms、20.476×**，未達30×。
- Median GPU-er-o **1161.959ms、5.124×**，九次皆超過同輪ASGP中位數的5×。
- CPU比本輪起點約快2倍，但仍比ASGP慢 **3.052×／7.640×**，未達接近ASGP。

搜尋時間包含冷啟動、admission／import、每代完整evaluate→selection→variation→compile/integrate、最後evaluation與AST export/admission。Process時間還包含input/grammar讀取、JSON輸出與獨立CPU稽核；兩者分開報告。GPU-o九個獨立冷process，其餘三個；這些搜尋沒有in-process warmup。正式量測未開NCU／NSYS，保留既有stage timers與診斷計數器。

| 模式 | House 起點→本輪（秒） | 相對ASGP | Median 起點→本輪（秒） | 相對ASGP |
|---|---:|---:|---:|---:|
| ASGP1T |27.736→27.628|1.00×|6.020→5.954|1.00×|
| GAGP CPU |186.022→84.328|0.328×|88.834→45.487|0.131×|
| GPU-e |19.083→9.739|2.837×|50.688→27.651|0.215×|
| GPU-er |2.253→1.355|20.384×|2.123→1.159|5.138×|
| GPU-er-o |2.245→1.349|20.476×|2.058→1.162|5.124×|

分母為同輪、同題、同代數ASGP1T三次中位數。起點是本輪開始的單次輕量重現，改善倍數不是精密配對估計。CPU counted-pair改RNG，CPU／GPU-e前後是不同populations，不能當成identical-workload executor speedup。

GPU-o範圍House1342.831–1373.432ms、Median1153.573–1175.795ms。GPU-er與-o差距接近噪音，不能宣稱此owner路徑額外享有顯著overlap收益。

**完整process**：ASGP中位House28.051s／Median6.494s，GPU-o2.580s／2.342s，因此比值 **10.872×／2.772×**，不是上表搜尋比值。所有原始repeats、RSS、品質見 [search-repeats.csv](search-repeats.csv)，每代資料見 [generations.csv](generations.csv)。

## 固定單代與scaling：獨立口徑

固定父代完整generation，3process×(1warmup+3measured)，中位House32.979800ms、Median7.342773ms。歷史frozen分母1282.011816／61.777141ms，對應 **38.873×／8.413×**。不能用它替代House跨代搜尋的20.476×。[九次原始數據](fixed-repeats.csv)。

[Scaling](scaling.csv) 所有控制組這輪重跑，沒有沿用受共用CPU修改影響的舊數字。P1024／2048／4096兩題五模式成功；House8192五模式成功。Median8192 ASGP／CPU／GPU-e成功，GPU-er及-o明確CUDA out-of-memory。同卡其他工作佔約18.5GiB，剩約5.6GiB；未動其他工作，也未改CPUfallback填表。Sum of Elements P1024另跑五模式smoke成功。Frozen hashes全部核對，沒有重新prepare。

## 本輪消除的工作與保留成本

| 方向 | 保留的改動 | 未消除的成本／邊界 |
|---|---|---|
| CPU reproduction | lazy analysis、單次reproduction內重用selected scalar parents、只建立選中的mutation site、compatible-group counted pairs | 新child仍canonical admission；registry payload與非local projected resources走相容路徑；仍是native AST reproduction |
| CPU evaluator | typed phases、binding tables、list/coordinate views、bounded dense memo、compact coordinate driver | 完整program/case與動態phase解譯仍存在 |
| GPU import/export | 批次scatter/gather取代大量小copy、immutable grammar/profile reuse、復用已admitted executable | 外部驗證、coldimport、finalexport均計時 |
| GPU executable | 私有packed carrier消除DInstr→Instr→DInstr及static metadata重建 | dynamic phases D2H/H2D、per-eval copy與metadata refresh仍需做；不是全resident executable |
| GPU reproduction | parallel offsetscan、launch配置、static weights、per-child mutation scratch、8-byte generator Task | 全GPU selection/crossover/mutation/lowering保留；scratch主要省60/120MiB，無可靠steady-state加速宣稱 |
| GPU executor | capability buckets／workspace、bound comparison、ring4 memo | generic fallback完整；fuel/error/memo容量順序保留；ring沒有縮小全域workspace allocation |

通用grammar不需要每代重證immutable static grammar、不需要重建未選中的sites，也不必讓簡單個體支付最複雜frame規格。本輪已有A/B支持這些改動；尚未證明所有canonical admission或generic VM成本都能消除。

## 語意、品質與長跑限制

新採用的搜尋軌跡改動只有 `GAGP_CPU_COUNTED_PAIRS=1`：逐pair reservoir RNG改成完整雙向合法pair計數後抽uniform rank。可行集合與條件分布不變；RNG消耗、seed軌跡改變。非local projected-resource trial保留舊路徑。數值、fuel、cases、資源限制、operator機率未改。影響CPU／GPU-e，不把GPU reproduction搬回CPU。

**品質反例必須保留**：Median選定seed2的CPU/e best-seen從舊軌跡−462變成−642；House seed0從−8380改善到−6544。三seed篩查Median新舊各1/3跳出−642，不足以證明收斂品質等價。[同wall-clock與錯誤率原始證據](../parallel-cpu-counted-pairs-20261013.json)。

本輪GPU runtime改動保持原GPU軌跡。正式30個主run的final export全部與先前獨立稽核的combined2 candidate一致。Best-so-far完整1024 training／獨立1024 heldout fitness如下，越大越好：

| 題目 | ASGP | CPU／GPU-e | GPU-er／-o |
|---|---|---|---|
| House |−4234／−4251|−6544／−6771|−6617／−6828|
| Median |−578／−651|−642／−682|−642／−682|

ASGP採native評分，沒有使用已知有負索引語意差異的port。兩者搜尋空間／執行語意並非完全相同。獨立checkpoint audit為25列、11unique programs，訓練分數重現；不能用速度掩蓋品質差距。

128代×3seed GPU-o：[long-search.csv](long-search.csv)。House best-seen[-4427,-768,-4535]，Median[-478,-642,-642]；六run均top16 CPU audit零差異、timeout/fallback/unscored/operator fallback為零。Median有正常計分的runtime errors，不是漏評。Final genotype unique House1022、Median957–1019，不能當成behavior diversity或每代unique new offspring。

長程有bloat：House seed0首8代中位24.04ms，末8代176.42ms，final mean750nodes；Median seed0為12.37→30.52ms、mean783nodes，其餘兩seed較停滯。不可用早期generation外推整個搜尋。

另做512代Median/seed0 memory診斷：搜尋17.160s、最佳−418、unique1018、top16CPU audit一致，GPU genotype高水位763.5MiB。搜尋中段RSS約1.0–1.06GiB，輸出階段process高水位2765.6MiB。Requested history／JSON仍成長；不宣稱任意長跑constant memory或無leak。[RSS原始採樣](memory-rss.csv) 從process啟動計時，不能與search-relative timestamps直接當成同一起點。

所有specialization由結構admission決定，無題名solver。非支援scope/variation/resources明確generic fallback。外部AST/bytecode仍驗證，內部owner不能偽造verified bit；型別正確不等於membership。Ring4只接受已證明單coordinate Int結果、下區間base與−1..−4 transitions；generic grammar能力保留。

## 最新成本診斷

正式run stage timer，首／末8代中位：

| GPU-o | House早／晚 ms | Median早／晚 ms |
|---|---:|---:|
| generation |23.84／30.71|9.63／11.01|
| eval整段 |20.18／24.12|3.92／3.75|
| eval kernel |18.45／22.65|2.33／2.45|
| repro整段 |4.01／6.52|5.28／6.88|
| repro GPU kernels |3.13／4.59|3.83／5.31|
| packing |0.25／0.48|0.40／0.41|
| repro decode/integration |0.65／0.89|0.89／0.94|

欄位有包含關係，不能相加。GPU lowering在repro kernel，compile_ms=0不代表沒compile。初始admission約House212／Median203ms，finaleval+export約187／140ms，已計搜尋。

最後4代NSYS只用診斷，含cold與finaleval：House eval平均19.64ms、GPU compile1.021、mutation0.990、crossover0.654；Median主要eval2.074＋genericbucket約0.234、mutation2.186、compile0.906、crossover0.503ms。[Kernel明細](profile-kernels.csv)，API/memcpy/完整trace在raw final-profile。不可從正式總時間直接扣profile時間。

前段focused NCU代表性House eval22.43ms、eligible warps0.168/cycle、active warps65.2%、DRAM54.7%、SM18.15%。Local-load sectors不是DRAM bytes，也不能直接判定register spilling；register-bank原型確實產生spill並退步，已移除。

CPU House主要仍是evaluator；Median evaluator和canonical reproduction都重。GPU-e Median受CPUrepro拖累。沒有把早期perf樣本百分比冒充最新精確share。

## 淘汰、測試與恢復

已移除runtime原型：size tie-break（搜尋退化）、CPU sampledsites／earlycompaction／single-hole copyelision／staticrootfilter、GPUsharedstack／cachedtop／registerbank、phasefuel-bound、diagnostic atomics。Historical patches/results保留，非可用入口。沒有刪掉共用失敗測試。

127/127整合native tests於compactTask前通過；compactTask整合後6個受影響test全過。Repository24，tools89（4skip）；focused CPUASan/UBSan與GPUmemcheck零錯誤。固定snapshot與歷史逐program相同（保留原有4個House CPU/GPU差異）。覆蓋wrongowner/inputtype fallback、mixedbucket、metadata growth/reset、scope/repeatedholes、projectedresources、fuel/capacity、負座標與2^53邊界。[驗證紀錄](validation.json)。

原始workspace checkpoint `75d7f83`，實驗起點 `9ddab1d`，runtime最佳 `b60a3367b37727bcf897be9cd66c27038da4d774`。Worktree `/home/hschi1106/.t3/worktrees/gagp/gagp-opt-20261001`；branch `opt/gpu-generation-20261001`。原始workspace／frozeninputs未更動，沒有push。Binary SHA256：`9962848fae0aec713e246bd3c106bcfe04bb0e91b380ce70140f586aa256b2e8`。

完整flags、input hashes、raw locations見 [final.json](final.json)。CPU1T、jemalloc background_thread關閉。於主worktree重現目前最終整合版五模式一次：

```bash
python3 - <<'PY'
import json, subprocess
from pathlib import Path
d = json.loads(Path('benchmarks/fixed_asgp/optimization/results/parallel-20261013/latest/final.json').read_text())
cmd = ['python3', 'benchmarks/fixed_asgp/optimization/short_search.py',
       '--binary', 'logs/optimization/parallel-20261013/followup-main/bench',
       '--out', 'logs/optimization/parallel-20261013/reproduce-new', '--repeats', '1']
for k, v in d['settings'].items():
    if k.startswith('GAGP_'): cmd += ['--flag', k + '=' + v]
subprocess.run(cmd, check=True)
PY
```

必須使用新outputdir。只看GPU-o加`--modes gpu_overlap`；正式GPU重複改9。重建最終摘要：`python3 benchmarks/fixed_asgp/optimization/results/parallel-20261013/summarize.py --followup`；不加參數則重建b60a336歷史checkpoint。Raw logs與archived binaries保留在 `logs/optimization/parallel-20261013/`。

## 最後一批受控實驗

- **保留 CPU all-fit compatible groups**（整合`2d7dac3`）：在exact counted-pair模式下以群組最大donor需求／最小receiver allowance證明雙向全部合法，直接計數與取rank；否則保留逐pair檢查。Projected allowances全部保留原掃描。三個paired processes GPU-e中位House9799.642→9602.729ms，Median27446.501→26525.440ms；較大late population無改善。RNG、全部fitness/variation counters/finalAST相同。新設定`GAGP_CPU_ALL_FIT_PAIRS=1`，不是另一條CPU reproduction架構。[原始證據](../parallel-cpu-all-fit-pairs-20261013.json)。
- **淘汰 lazy typed LOAD binding**：House4搜尋12.592→12.768s，late Median evaluator1.200→1.233s；重複LOAD取binding抵銷prebinding消除的工作。程式碼與開關已移除。[證據](../cpu-lazy-load-rejected-20261013.json)。
- **淘汰GPU per-slot executable reuse**：private owner lineage、double-buffer certificate、edit時invalidate，40代正確性對照通過。三process House改善0.63%、Median0.69%，範圍重疊；實際減少部分GPUcompile卻未建立有價值E2E收益。整個prototype/flag移除，raw patch保留為歷史紀錄。[證據](../parallel-gpu-20261013/rejected-executable-reuse.json)。

三個探索worktree：`parallel-cpu-eval-20261013`、`parallel-cpu-repro-20261013`、`parallel-gpu-20261013`（均位於主worktree同一父目錄）；最後統一在主worktree驗證。CPU1T不是禁止平行開發，但所有性能run與build均以同一lock序列化。

目前沒有證據支持繼續堆疊這些小型cache／binding／stack變體。House30×與CPU接近ASGP仍未完成：下一個值得重新建立證據的方向是static typed phase execution plan，必須先證明比既有typed VM快，且把每代新phase建構成本算入；不能直接假定JIT/codegen會贏。Median後期則仍需處理canonical admission與population bloat，但已淘汰的size tie-break會使搜尋退化，不能原樣重用。
