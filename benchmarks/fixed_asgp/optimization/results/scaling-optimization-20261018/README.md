# 大population與全30題GPU優化（2026-10-18，進行中）

新授權24h，固定2026-10-18 11:18:51 UTC至2026-10-19 11:18:51 UTC（台灣10/19 19:18:51）；不延長或重設。舊quality-control與day24已完成，保持不變。

[目前結果怎麼讀（進行中）](CURRENT.zh-TW.md)：列出P／代數、已完成筆數、GPU搜尋與額外CPU核對時間；正式加速表尚未產生。

先做Is Sorted／All Equal／House Robber的P16384、完整1024cases、G8容量／全population CPU fitness+wrong核對，使用既有frozen inputs、不替換個體。比較ASGP／原GPU／tuple execution-only；保留16GiB workspace、2048/depth24/phase1024與GPU評分及繁殖。再依未profile實測與nsys診斷優化，凍結正式版本／共同代數與重複數後完成全30題P1024／16384，同輪量測分母。品質診斷使用固定父代／操作另列，不把算子變更混為執行加速。

[凍結起點與約束](protocol.json)。Raw、manifests、pinned binaries在`logs/scaling-optimization-20261018/`；每stage單一controller、所有重工作共用`/tmp/gagp-support-20261015-gpu.lock`。No subagents／push。正式輸出與易讀報告尚未產生。

啟動前harness檢查attempt0因外層與toy子工作重複取得同一flock而卡住；確認沒有benchmark子行程後停止該unittest。測試改用內層暫存鎖，attempt1四項通過；兩份log均保留。沒有刪除／重跑任何正式或pilot樣本。Pilot啟動時的harness原檔保存在raw的`harness-source-at-launch/`。

新暫時monitor每30分鐘返回本thread，首次2026-10-18 12:01:17 UTC（台灣20:01）；ID／prompt另存raw同層`scaling-optimization-20261018-monitor.json`，最終交付或取消時停用。兩個舊實驗monitor維持停用。

`development.py`（PID3197591）只等待pilot與全部children退出且9runs／3組完整AST比較通過，之後才在共用鎖下做harness檢查、Release benchmark build、adapter回歸、兩筆nsys與40筆development測速。固定五題IsSorted／AllEqual／HouseRobber／FilterOdd／DoubleLetters，各P1024／16384、G8、blocksize512／256／128／64；tuple-only開啟，不改算子。選一個全局blocksize，要求10格CPU核對／全最終AST／descriptors／champion fitness一致，且任一格退步不超過10%，再比幾何平均。這是development候選，不自動啟動正式矩陣。

正式runner為`formal.py`，尚未執行，必須先新增不可變`formal-freeze.json`。依[時間分配規則](allocation-policy.json)選共同G／重複數；正式rep0的兩GPU方法每題／每P均核對全部最終個體，其餘reps核對前16，CPU核對成本另列；全程保存成功與失敗狀態。目前沒有正式加速比可報告。

Pilot9/9與原development40/40均完整結束。較小分組皆未通過全局速度門檻；256另有IsSorted P16384 final score descriptors差異，完整AST仍相同／前16 CPU audit0差異，並未當作通過。新增[1024分組development規則](extension1024-plan.json)與`extension1024.py`，在原controller退出後用同一新binary另跑512／1024二十筆、保留所有原結果；只依training runtime／一致性選單一全局設定，尚未正式freeze。最後才於共用鎖下產生nsys統計及靜態CUDA binary資源資料，不與搜尋並行。
