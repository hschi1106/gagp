# 開發期 GPU 瓶頸觀察

舊training、P1024、G64、seed0，以本輪起始最快GAGP執行nsys。這4題是開發診斷，不能代表全30題。

| 題目 | 主評分kernel占GPU kernel時間 | 突變kernel占GPU kernel時間 |
|---|---:|---:|
|is_sorted|96.7%|1.8%|
|double_letters|96.4%|2.3%|
|median|16.0%|55.4%|
|tribonacci|92.4%|4.0%|

- IsSorted、DoubleLetters、Tribonacci的這次GPU時間主要在評分；Median主要在突變。只加快評分不能處理所有題目的瓶頸。
- 這是GPU kernel時間占比，含profiler開銷；不是整體搜尋時間占比，也不能據此宣稱記憶體頻寬已飽和。四份NVTX報表都明確記錄沒有NVTX資料，沒有據此建立host階段時間比例。
- 加速比使用正式未profile的搜尋wall time，包含GPU評分與GPU繁殖；不得拿這張表的秒數當作正式分母。
- 私有list已有以單個64-bit word運算的bit-parallel edit distance，位於short_edit_distance.hpp。本輪layout8也實測過short arm，增益不足而未採用；不能把同一方法再列為新的已驗證改進。

[原始比例與kernel呼叫次數](development_profile.csv)；來源CSV、manifest及輸出SHA見development_profile_evidence.json。
