# ASGP 1T / GAGP GPU-er-o：完整30題 scaling

固定16 GiB workspace，GPU完整執行與驗證 **149/150**；合格加速比 **149/150**。
P=16384已有 **29/30** 題通過全部16384個最終個體的CPU fitness audit；GPU成功格皆零 mismatch。
整張RTX3090（24 GiB）的NVML取樣峰值為 **21.27 GiB**，含桌面背景。

## 每種population

中位數、幾何平均與範圍均為該population跨題目的倍率統計。

| Population | GPU通過 | 快於ASGP | 加速比中位數 | 幾何平均 | 最小–最大 |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1024 | 30/30 | 27/30 | 9.77× | 7.12× | 0.80–31.86× |
| 2048 | 30/30 | 30/30 | 12.61× | 8.76× | 1.08–42.55× |
| 4096 | 30/30 | 30/30 | 14.89× | 10.04× | 1.25–46.61× |
| 8192 | 30/30 | 28/30 | 13.23× | 9.32× | 0.66–43.55× |
| 16384 | 29/30 | 27/29 | 13.90× | 9.92× | 0.80–47.60× |

## 全部題目的加速比

| 題目 | 1024 | 2048 | 4096 | 8192 | 16384 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Filter Odd | 9.32× | 12.28× | 14.40× | 11.98× | 13.90× |
| Sum Of Elements | 20.80× | 24.76× | 30.39× | 30.29× | 33.37× |
| Is Sorted | 1.33× | 1.68× | 1.87× | 2.08× | 2.18× |
| Index Of Element | 11.57× | 13.43× | 16.08× | 16.04× | 16.28× |
| All Equal | 1.28× | 1.60× | 1.89× | 2.00× | 2.16× |
| Cumulative Sum | 13.04× | 15.90× | 19.24× | 15.29× | 17.07× |
| Fuel Cost | 20.23× | 23.87× | 30.00× | 29.50× | 31.83× |
| Luhn | 9.65× | 13.94× | 16.06× | 15.37× | 16.06× |
| Min | 22.23× | 27.94× | 36.85× | 36.75× | 38.94× |
| Mpi | 17.55× | 22.60× | 25.30× | 26.10× | 28.36× |
| Count Odds | 18.69× | 24.21× | 27.15× | 28.14× | 29.06× |
| Last Index Of Zero | 9.60× | 11.26× | 12.34× | 12.38× | 13.02× |
| Small Or Large | 0.93× | 1.13× | 1.32× | 0.67× | 0.95× |
| For Loop Index | 7.84× | 11.17× | 12.82× | 9.62× | 11.49× |
| Double Letters | 15.06× | 17.48× | 17.93× | 16.89× | 18.03× |
| Replace Space With Newline | 11.34× | 12.52× | 14.25× | 13.28× | 13.87× |
| Even Squares | 11.53× | 15.00× | 15.38× | 13.18× | 14.60× |
| Sum Of Squares | 13.81× | 17.74× | 21.74× | 22.23× | 23.69× |
| Negative To Zero | 12.39× | 16.78× | 18.32× | 15.60× | 18.17× |
| Scrabble Score | 29.61× | 39.22× | 41.47× | 43.03× | 46.31× |
| Grade | 1.21× | 1.08× | 1.25× | 0.66× | 0.80× |
| Median | 0.89× | 1.14× | 1.58× | 1.27× | 1.45× |
| Smallest | 0.80× | 1.14× | 1.30× | 1.26× | 1.43× |
| Syllables | 31.86× | 42.55× | 46.61× | 43.55× | 47.60× |
| Fibonacci | 6.36× | 7.37× | 8.01× | 8.81× | 8.71× |
| Tribonacci | 4.92× | 4.88× | 5.24× | 5.23× | 5.30× |
| House Robber | 9.89× | 12.71× | 15.77× | 16.73× | initial_admission_limit |
| Subregion Sum | 2.96× | 3.71× | 4.38× | 4.84× | 4.57× |
| Fibonacci Array | 5.38× | 6.11× | 6.26× | 6.48× | 6.56× |
| Stock With Cooldown | 5.68× | 6.27× | 6.68× | 6.77× | 6.99× |

## 執行限制與驗證範圍

- house_robber P=16384：GPU initial_admission_limit、ASGP ok；初始 frozen parent index8603 轉譯為1086個AST節點，超過固定max_nodes=1024（depth24）。只放寬depth至48仍失敗；nodes4096/depth24僅用於離線診斷，未用於主表。這是admission限制，尚未進入GPU。沒有合格倍率。

- 每格一次獨立冷啟動；完整1024 cases、CPU1T、seed0、四代與最後評分。倍率是ASGP `evolve_wall_ms` / GAGP `evolve_wall_ms`，包含初始化、必要materialization與清理；CLI I/O、export及獨立CPU audit另列。這是支援與粗略速度測試，不是精確重複量測或收斂等價證明。
- 所有GPU格使用同一binary與16GiB設定。P16384對全部最終個體做CPU fitness核對；較小P保留top16 CPU audit，並比較完整輸出AST、fitness及每代semantic/operator traces。Runtime errors/timeouts是隨機個體的評分結果，與整個執行是否成功分開計數。
- 原phase owner的8192上限已擴充至16384，仍保留12GiB genotype guard。記憶體不足以同時保留全部暫存時，評分保留當代genotype；繁殖暫存與評分scratch分階段配置及釋放。搜尋參數、RNG、工作量與16GiB scratch上限維持原設定。一般reproduction的100萬項metadata guard未抬高。
- Native tests 131/131通過；tools tests 115項（4 skips）、repository checks 24項通過。強制staged/resident與in-place/out-of-place檢查含32代完整AST、CPU/GPU fitness及案例／錯誤計數。
- ASGP邏輯未改，同一population、inputs與settings的原有效控制組重用；原先未完成的P16384控制另測。沒有使用其他population推算分母，也未把8GiB的時間混入主表。舊8GiB結果只作較小P的完整語意比較；原16GiB失敗與probe全部保留。
- NVML每200ms取樣整張卡，可能漏掉較短的尖峰；16GiB是scratch上限，與genotype峰值分屬不同生命週期，不能直接相加當成實際同時顯存用量。

## 重現與資料

[執行入口與恢復方法](../README.md) · [全部原始秒數與counters](cells.csv) · [逐代資料](generations.csv) · [互動表](dashboard.html) · [英文圖表](scaling.png) · [PDF](scaling.pdf) · [硬體與設定](environment.json) · [來源雜湊](provenance.json)

Runtime checkpoint：`356bc55`；binary SHA256：`2170bb7dba9af579fc89480df22681adbc4b7d4e3e4f598e858b0a8422ffe8cb`。
完整原始資料：`/home/hschi1106/.t3/worktrees/gagp/gagp-support-20261015/logs/all30-scaling-20261016/same16`；manifest SHA256：`01d34625b065fa29a5309bf766badf78052ad2020ad42f4ca647ad8be74ff031`。
