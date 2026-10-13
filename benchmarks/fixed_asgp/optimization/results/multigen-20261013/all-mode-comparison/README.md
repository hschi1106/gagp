# ASGP 與全部 GAGP 模式比較圖

圖中文字均為英文；所有資料取自既有 CSV，未重跑或更改 benchmark。

1. [Fitness vs. generation](01_fitness_vs_generation.png)：三題、五模式，每代當代 best fitness 的三 seed 中位數，除以1024；越高越好。不是 best-so-far。完全重合的模式以不同線型與錯開的 marker 表示。
2. [單代時間與後期成本](02_generation_time_all_modes.png)：上排為共同16代的三 seed 中位數；下排為 seed0 的完整可用搜尋，沒有延長資料者只畫16代。時間用 log 軸，下排代數用 generation+1 的 log 軸。不同長度不外推。
3. [GAGP CPU 加速比](03_speedup_gagp_cpu.png)
4. [GPU-e 加速比](04_speedup_gpu_eval.png)
5. [GPU-er 加速比](05_speedup_gpu_repro.png)
6. [GPU-er-o 加速比](06_speedup_gpu_overlap.png)

加速比每代一柱，定義為 ASGP 該代時間的三 seed 中位數 ÷ GAGP 該代時間的三 seed 中位數。大於1代表GAGP較快；小於1代表ASGP較快。四張圖均用全部模式共同量測的generation 0–15，不用不同run／seed拼接分母。

單代時間包含完整評估、selection與reproduction／下一代準備；一次性初始化及final-only evaluation不在逐代比值內。這些比值不是整趟搜尋加速比。各模式演化出的population不同，亦非固定程式的executor比較。ASGP採原生評分；對齊fitness公式不表示語言、operator或RNG相同。Sum初始即有解，不能證明搜尋優勢。

每張 PNG 均有同名 SVG。可用 [plotted_values.csv](plotted_values.csv) 核對主比較數值；[manifest.json](manifest.json) 記錄來源SHA256、口徑與長跑覆蓋代數。

重畫命令（repo root）：

```bash
python3 benchmarks/fixed_asgp/optimization/multigen_comparison_plots.py --results benchmarks/fixed_asgp/optimization/results/multigen-20261013
```
