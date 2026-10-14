# 三題五模式重跑 — 2026-10-14

P=1024、完整 1024 cases，CPU1T，runtime `be455a4`，沿用最佳完整 flags；不改 runtime。Sum／House 各32代seed0，Median64代seed2。每格3個獨立process，包含cold start，沒有開profiler。

## 完整搜尋時間

各模式時間為3次中位數（秒）；最後兩欄使用**本輪相同題目的ASGP搜尋時間**。

| Task | ASGP 1T | CPU | GPU-e | GPU-er | GPU-er-o | Fastest GPU | ASGP / fastest GPU |
|---|---:|---:|---:|---:|---:|---|---:|
| sum_of_elements | 49.056 | 250.991 | 10.253 | 4.332 | 4.339 | gpu_repro | 11.32× |
| house_robber | 27.669 | 82.200 | 8.476 | 1.043 | 1.045 | gpu_repro | 26.53× |
| median | 5.826 | 42.051 | 24.118 | 0.856 | 0.858 | gpu_repro | 6.81× |

沿用歷史目標分母時，GPU-er-o：house_robber 26.49×、median 6.84×。Sum32使用本輪controls，不借用舊16代分母。


搜尋邊界含冷啟動、必要admission/import、所有generation與最終evaluation/native export。額外輸入解析、audit、history／JSON輸出包含在另列process wall，不混入搜尋倍率。GPU-er與overlap的小差距不可當成可靠的overlap獨立收益。

## Fixed-parent完整單代

每格獨立process：warmup一次後量測三次，單位ms；各題使用本輪自己的ASGP單代分母，**不可用來宣稱跨代搜尋達標**。

| Task | ASGP 1T | CPU | GPU-e | GPU-er | GPU-er-o | ASGP / GPU-er-o |
|---|---:|---:|---:|---:|---:|---:|
| sum_of_elements | 1897.124 | 5560.676 | 249.708 | 45.902 | 45.844 | 41.38× |
| house_robber | 1298.206 | 3362.097 | 261.602 | 31.085 | 30.940 | 41.96× |
| median | 56.968 | 515.805 | 234.078 | 6.693 | 6.604 | 8.63× |

## 各模式搜尋品質與前後段成本

Fitness越高越好，0為訓練集完全符合；單一seed只供這次性能結果的品質背景，不代表收斂排名。不同架構可能走不同軌跡；原有數值／執行profile差異仍存在，不能把所有耗時差當成identical-workload速度差。前後段為3次搜尋的前8／後8代單代中位數。

| Task | Mode | Final fitness | Early gen ms | Late gen ms | Late / early | Process s | Process speedup |
|---|---|---:|---:|---:|---:|---:|---:|
| sum_of_elements | asgp_1t | 0 | 1934.649 | 1044.536 | 0.54 | 49.557 | 1.00× |
| sum_of_elements | gagp_cpu | 0 | 6802.679 | 7856.430 | 1.15 | 252.255 | 0.20× |
| sum_of_elements | gpu_eval | 0 | 224.795 | 415.231 | 1.85 | 11.544 | 4.29× |
| sum_of_elements | gpu_repro | 0 | 73.099 | 160.527 | 2.20 | 6.121 | 8.10× |
| sum_of_elements | gpu_overlap | 0 | 72.617 | 159.502 | 2.20 | 6.092 | 8.13× |
| house_robber | asgp_1t | -4234 | 689.113 | 879.659 | 1.28 | 28.110 | 1.00× |
| house_robber | gagp_cpu | -6544 | 2102.384 | 2786.530 | 1.33 | 83.418 | 0.34× |
| house_robber | gpu_eval | -6544 | 170.615 | 386.235 | 2.26 | 9.742 | 2.89× |
| house_robber | gpu_repro | -7146 | 22.639 | 28.100 | 1.24 | 2.283 | 12.31× |
| house_robber | gpu_overlap | -7146 | 22.688 | 28.069 | 1.24 | 2.290 | 12.27× |
| median | asgp_1t | -578 | 47.808 | 143.754 | 3.01 | 6.391 | 1.00× |
| median | gagp_cpu | -642 | 601.953 | 621.219 | 1.03 | 43.264 | 0.15× |
| median | gpu_eval | -642 | 312.664 | 340.117 | 1.09 | 25.377 | 0.25× |
| median | gpu_repro | -642 | 8.434 | 9.511 | 1.13 | 2.051 | 3.12× |
| median | gpu_overlap | -642 | 8.393 | 9.369 | 1.12 | 2.048 | 3.12× |

Sum 的 frozen 初始 population 在 generation0 已有 fitness0；這個32代實驗驗證持續演化的執行成本，**不能解讀為從未解到解出的搜尋能力比較**。沒有提前停止、替换初始輸入或跳過完整評估。


## 原始重複量測

| Task | Mode | Search repeats ms | Range ms |
|---|---|---|---|
| sum_of_elements | asgp_1t | 49055.552175, 48564.653421, 49079.795711 | 48564.653–49079.796 |
| sum_of_elements | gagp_cpu | 251501.297055, 248697.023904, 250990.646802 | 248697.024–251501.297 |
| sum_of_elements | gpu_eval | 10169.199181, 10253.344302, 10274.252991 | 10169.199–10274.253 |
| sum_of_elements | gpu_repro | 4331.542988, 4335.902960, 4331.632197 | 4331.543–4335.903 |
| sum_of_elements | gpu_overlap | 4322.672830, 4339.177564, 4340.619473 | 4322.673–4340.619 |
| house_robber | asgp_1t | 27884.544238, 27669.070759, 27435.439635 | 27435.440–27884.544 |
| house_robber | gagp_cpu | 81838.258354, 82199.558521, 82568.818650 | 81838.258–82568.819 |
| house_robber | gpu_eval | 8289.054132, 8476.407611, 8562.973721 | 8289.054–8562.974 |
| house_robber | gpu_repro | 1041.213002, 1043.042199, 1046.927493 | 1041.213–1046.927 |
| house_robber | gpu_overlap | 1045.018549, 1044.438645, 1046.158714 | 1044.439–1046.159 |
| median | asgp_1t | 5826.117187, 5825.205090, 5879.655119 | 5825.205–5879.655 |
| median | gagp_cpu | 42142.730409, 42051.142434, 41996.797590 | 41996.798–42142.730 |
| median | gpu_eval | 24118.241518, 24077.258083, 24281.512712 | 24077.258–24281.513 |
| median | gpu_repro | 862.501633, 851.619488, 856.060872 | 851.619–862.502 |
| median | gpu_overlap | 857.362356, 857.690179, 862.131219 | 857.362–862.131 |

所有binary／input SHA、flags、命令與raw repeats見[summary.json](summary.json)，逐代fitness／time／errors／size等見[generations.csv](generations.csv)。原始資料在 `logs/optimization/three-task-20261014/`。CPU audit覆蓋範圍依各run記錄，不宣稱全部programs皆獨立CPU重評。


重現（新輸出目錄；不prepare原始輸入）：

```bash
python3 benchmarks/fixed_asgp/optimization/results/three-task-20261014/run.py --out logs/optimization/three-task-NEW
python3 benchmarks/fixed_asgp/optimization/results/three-task-20261014/summarize.py --raw logs/optimization/three-task-NEW --out logs/optimization/three-task-NEW/summary
```

[完整搜尋比較圖](search-comparison.png)（英文標示、對數時間軸）／[SVG](search-comparison.svg)。
