# Packed-frame追加開發結果

本表使用舊training，P1024／G64／seed0，每格一次。原正式結果仍為1.079×，未達1.3×。

預訂150次搜尋，實際建立0次；程序OK0次；5臂完整核對0/30題。
隔離回歸：未通過／未完成。控制器狀態：stopped；條件式確認：not_run_prerequisite_failed。

倍率＝本次新測selected-control搜尋時間÷本次新實作搜尋時間。完整30題才列幾何平均，失敗與未執行不刪除。

停止原因：`RuntimeError('build exceeded original budget')`。建置狀態：`timeout`；native狀態：`not_run`。
建置逾時不提供語意、演化品質或加速證據；後續未執行的格子保留於CSV。

- ordinary：未取得完整合格對照。
- packed_aos：未取得完整合格對照。
- packed_soa：未取得完整合格對照。

相對這次新測beb5585d原基準（仍為舊training開發對照）：

- ordinary：未取得完整合格對照。
- packed_aos：未取得完整合格對照。
- packed_soa：未取得完整合格對照。

[逐次紀錄](runs.csv) · [逐題對照](tasks.csv) · [彙總](results.json)

這份開發表沒有新增heldout、ASGP分母或5pp非劣性證據，也不替換正式報告。數據來自未profile的搜尋wall；cold process與判題另計。
