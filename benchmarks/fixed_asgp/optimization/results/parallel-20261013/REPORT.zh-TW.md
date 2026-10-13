# 平行 worktree 優化結果（2026-10-13）

此為已驗證的 `b60a336` checkpoint；最後一批受控 follow-up 的採用決定會另列，不把未量測原型當成最佳版本。

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

完整flags、input hashes、raw locations見 [final.json](final.json)。CPU1T、jemalloc background_thread關閉。於主worktree重現五模式一次：

```bash
python3 - <<'PY'
import json, subprocess
from pathlib import Path
d = json.loads(Path('benchmarks/fixed_asgp/optimization/results/parallel-20261013/final.json').read_text())
cmd = ['python3', 'benchmarks/fixed_asgp/optimization/short_search.py',
       '--binary', 'logs/optimization/parallel-20261013/final-main/bench',
       '--out', 'logs/optimization/parallel-20261013/reproduce-new', '--repeats', '1']
for k, v in d['settings'].items():
    if k.startswith('GAGP_'): cmd += ['--flag', k + '=' + v]
subprocess.run(cmd, check=True)
PY
```

必須使用新outputdir。只看GPU-o加`--modes gpu_overlap`；正式GPU重複改9。重建摘要：`python3 benchmarks/fixed_asgp/optimization/results/parallel-20261013/summarize.py`。Raw logs與archived binaries保留在 `logs/optimization/parallel-20261013/`。
