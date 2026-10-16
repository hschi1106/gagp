# ASGP 1T / GPU-er-o：30題完整G4 scaling檢查

固定CPU1T、完整1024cases、seed0、四代與最後評估，沿用`evolve_wall_ms`。P=1024/2048/4096/8192/16384，每格單次cold process；不是精確重複矩陣或品質等價證明。GPU測試依序，ASGP controls也依序，不與offline preparation搶CPU。

Runtime binary：`logs/list-5x-20261015/pooled16-ten/bench`（source b1760de，production b568c7e）。設定：上一輪`settings-workspace-pool16.json`，一個application CPU thread。先測16GiB；只有allocation failure才明列重試8／4GiB，不減population、cases或世代。每次attempt保存獨立log／JSON，不把失敗時間當有效speedup。重試是外部重新設定後的獨立run，不是假裝已包含失敗成本的自動fallback。

以固定ASGP seed產生新的16384-parent source，驗證前1024parents及全部cases對舊frozen artifact一致。新的native preparation驗證原1024 AST prefix及grammar hash；所有準備只寫新目錄。各P使用同一大artifact的prefix，不複製1024個體來填滿大population。

P1024已用最佳binary重跑全部30題GPU；這批有效GPU runs在正式矩陣中可依binary／settings／inputs hash重用。正式矩陣的五種P均重新測同P的ASGP1T control，不能拿歷史P1024分母套到其他P。`p1024/`只保留先期使用歷史分母的結果，不是最終scaling表。

```bash
PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/scaling-all30-20261016/run_matrix.py prepare \
  --out logs/all30-scaling-20261016/artifacts
PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/scaling-all30-20261016/run_matrix.py measure \
  --out logs/all30-scaling-20261016/matrix \
  --artifacts logs/all30-scaling-20261016/artifacts
```

恢復同一階段：原命令加`--resume`。先確認沒有同階段controller仍在執行；新版runner有controller flock，拒絕重複啟動。prepare最初執行版另存`logs/all30-scaling-20261016/prepare-runner-v1.py`，只是少了後來新增的controller lock與measure恢復保護，準備內容／驗證不變。

Raw狀態：`logs/all30-scaling-20261016/{artifacts,matrix}/manifest.json`。本輪outer launcher等待原prepare PID退出，確認30題完成後自動進入matrix；記錄於`logs/all30-scaling-20261016/launcher.json`。中斷後若matrix尚未開始，依上述第二條命令啟動；已存在則加`--resume`。所有完整結果落盤後再產生最終CSV／報告；計畫中的表格不表示已完成。

每cell1800s process limit、32GiB外部hostRSS保護；timeout／memory limit／OOM／admission failure／CPU audit mismatch明列，不產生合格speedup。完整Search含必要內部materialization／清理，CLI解析與export／獨立CPU audit依原邊界另列process成本。保留原有ASGP／GAGP搜尋契約差異，不宣稱grammar language／RNG／演化軌跡完全相同。


報表（新目錄）：

```bash
PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/scaling-all30-20261016/report.py \
  --matrix logs/all30-scaling-20261016/matrix \
  --out benchmarks/fixed_asgp/optimization/results/scaling-all30-20261016/final
```

`cells.csv`含每題／P的status、ASGP及GPU秒數、ratio、實際workspace及fitness／case counters；`generations.csv`保留逐代原始資訊。失敗或pending一律不計ratio。報表以真實完成JSON與真實constant-preparation-limit失敗log做過工具檢查。圖中文字用英文。

已知先期P16384 Filter容量檢查：16／8／4GiB都在演化前觸發constant node origin preparation item limit；不是已證實VRAM OOM。此probe與offline preparation可能重疊，只是support evidence，其時間不重用於正式矩陣。Runtime安全上限維持原樣，正式矩陣會重新記錄此失敗。

### P8192 launch failure診斷與獨立容量設定

Filter Odd的16GiB失敗已用nsys CUDA API trace確認`cudaLaunchKernel_v7000 returnValue=2`；本機CUDA12.6 `driver_types.h`定義為`cudaErrorMemoryAllocation`。trace與SQLite保存在`logs/all30-scaling-20261016/p8192-launch-diagnostic/`；profiled run不計ratio。不把其他只有相似文字的失敗直接宣稱為已證實OOM。

`capacity_retries.py`只對原16GiB出現同一tiled-initial-payload launch signature的cell，在共享鎖下分開量測8／4GiB設定。這是新的完整G4 run，不修改runtime、不刪原失敗，也不把失敗probe的耗時當作成功設定的耗時。實際通過完整cases／GPU repro／CPU audit才能填ratio；constant preparation limit不適用這個重試。

```bash
PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/scaling-all30-20261016/capacity_retries.py \
  --matrix logs/all30-scaling-20261016/matrix \
  --out logs/all30-scaling-20261016/capacity-retries --controller-pid <ACTIVE_MATRIX_PID>
PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/scaling-all30-20261016/report.py \
  --matrix logs/all30-scaling-20261016/matrix \
  --capacity-results logs/all30-scaling-20261016/capacity-retries/manifest.json \
  --out benchmarks/fixed_asgp/optimization/results/scaling-all30-20261016/final-selected
```

報表保留`gpu_original_status`／`gpu_original_log`與實際workspace，原始matrix不受覆寫。

`p1024-current/`與`p8192-checkpoint/`是執行中快照，包含尚未完成的pending欄位，僅供歷史／恢復；最終以`final-selected/`為準，並同時保留原16GiB矩陣`final/`。


### 更新後的固定16GiB主矩陣

使用者要求先檢查正確性，所有P固定16GiB；無法執行才列限制。`356bc55`
將native phase population擴至16384，按實際記憶體估算分階段釋放repro暫存及eval
scratch，保留12GiB genotype guard及全部搜尋參數。完整native131tests通過。
Filter P16384先期probe已通過全部16384個最終個體的CPU fitness audit，device-total
VRAM取樣峰值21766MiB；它與build重疊，只作正確性證據，不重用其正式時間。

```bash
PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/scaling-all30-20261016/run_same16.py \
  --out logs/all30-scaling-20261016/same16 --populations 1024 2048 4096 8192 16384
```

恢復同一命令加`--resume`，先確認`same16-controller.json`的PID已退出。
新runner有controller flock，保留中斷attempt及原log；完整的新attempt才取得結果。
每格使用同一candidate binary、固定16GiB、CPU1T、1024cases、G4+finaleval。
P16384在計時後對全部最終個體做完整cases CPU fitness audit；其餘P保留top16
CPU audit，並與舊版本有效資料比較全部輸出AST、fitness及semantic/operator traces。
舊8GiB資料僅供語意比較，不作新主表時間。ASGP邏輯未變，同inputs/settings的原
同P controls經驗證後重用，未完成controls另行實測。NVML只讀取樣記錄整張卡的
用量，含桌面背景，並非精確的process-only峰值。

新原始資料`logs/all30-scaling-20261016/same16/`；launcher及log為
`same16-controller.json`／`same16-controller.log`。主表完成後以`report.py --matrix`
指向`same16/`，不傳`--capacity-results`。此表取代不同workspace合併表作主結論；
原matrix、失敗及8GiB補測仍保留。
