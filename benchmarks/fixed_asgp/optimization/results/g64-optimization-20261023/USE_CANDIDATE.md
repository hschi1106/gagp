# 執行本輪G64候選設定

工具回歸及P1024／P16384的實際dry-run已通過；[preset](candidate_preset.json)已匯出，[PRESET_CHECK.json](PRESET_CHECK.json)保留輸入／binary設定與檢查結果。沒有啟動額外搜尋。首次檢查的wrapper漏設PYTHONPATH、在import前退出；失敗紀錄另存preset-dryrun-attempt0.json，更正版只補上文件中的啟動環境。

這份設定固定使用本輪candidate binary與完整環境設定，預設64代、CPU host worker1T、GPU評分與繁殖、16GiB workspace。原生實體容量為AST2048／depth24／phase1024。它對應本輪候選，不代表1.3×已達標。

在實際worktree根目錄執行。可直接使用已匯出的配置；若要另存一份，以下命令的`--out`須改成尚不存在的路徑：

```bash
PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/g64-optimization-20261023/candidate_preset.py export \
  --out benchmarks/fixed_asgp/optimization/results/g64-optimization-20261023/candidate_preset.json
```

下面使用同一份IsSorted／P1024初代與200個training cases。`--dry-run`只列出command與設定：

```bash
PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/g64-optimization-20261023/candidate_preset.py run \
  --preset benchmarks/fixed_asgp/optimization/results/g64-optimization-20261023/candidate_preset.json \
  --source logs/g64-optimization-20261023/formal0-inputs-is_sorted/inputs/is_sorted/s35.train.source.json \
  --prepared logs/g64-optimization-20261023/formal0-inputs-is_sorted/inputs/is_sorted/s35.prepared.json \
  --grammar logs/nodes2048-20261016/is_sorted/grammar.json \
  --population 1024 --generations 64 --cases 200 --seed 35 \
  --out logs/g64-user-example/is_sorted-p1024.json --dry-run
```

移除`--dry-run`可執行一次新搜尋。輸出包含結果、完整最終族群、冠軍history、log及receipt。已存在的輸出與失敗記錄會保留；receipt記錄本次binary、輸入hash與實際設定。單次程序完成只表示執行結束，解題與CPU分數一致性另需判題。

P16384使用自己的初代與prepared檔，替換上述三個參數：

```text
--source logs/g64-optimization-20261023/formal0-secondary-inputs/is_sorted.s35.p16384.source.json
--prepared logs/g64-optimization-20261023/formal0-secondary-inputs/is_sorted.s35.p16384.prepared.json
--population 16384
```

其他參數保留；輸出改用新的檔名，例如`logs/g64-user-example/is_sorted-p16384.json`。不同題目的cases數以自己的fixture為準，工具要求明確指定，沒有1024或200的隱含預設。

實際執行會共用GPU lock並核對binary與allocator hash；ambient的GAGP環境變數會由preset完整設定取代。新輸出需放在本輪正式logs目錄外，方便區分自己的執行與已凍結的實驗。這份preset依賴本機保留的binary、fixtures及allocator路徑，跨機器使用須另行重建並記錄來源。
