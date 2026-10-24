# 同代完全相同個體分組追加開發結果

本表使用舊training，P1024／G64／seed0，每格一次。原正式結果仍為1.079×，未達1.3×。

預訂120次搜尋，實際建立120次；程序OK120次；4臂完整核對30/30題。
隔離回歸：通過。控制器狀態：development_complete_only；條件式確認：not_run_incremental_gain_below2pct。

倍率＝本次新測selected-control搜尋時間÷本次新實作搜尋時間。完整30題才列幾何平均，失敗與未執行不刪除。

- ordinary：1.015108×
- current_groups：0.986011×

相對這次新測beb5585d原基準（仍為舊training開發對照）：

- ordinary：1.073729×
- current_groups：1.042952×

[逐次紀錄](runs.csv) · [逐題對照](tasks.csv) · [彙總](results.json)

這份開發表沒有新增heldout、ASGP分母或5pp非劣性證據，也不替換正式報告。數據來自未profile的搜尋wall；cold process與判題另計。
