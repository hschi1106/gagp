# 單機24小時 ASGP／GAGP 實驗（2026-10-17，已完成）

已完成正式速度G8的150/150配對格、品質P1024/G64的300/300 runs及600/600原生CPU train/test判題，均無程序失敗。控制器共9.234h；包含報告核對的總時間見[completion.json](final/completion.json)。[主要結論與限制](final/FINDINGS.zh-TW.md)、[完整逐題報告](final/REPORT.zh-TW.md)、[互動表](final/dashboard.html)、[速度CSV](final/speed.csv)、[品質CSV](final/quality.csv)。

正式規格見[protocol.json](protocol.json)。使用者已授權開始；總時間從2026-10-17 13:16:49 UTC起算24小時，包含準備、試跑、搜尋及核對，不因重啟controller重設。

- 速度：全30題×P1024/2048/4096/8192/16384，ASGP1T與GPU-er-o，seed0，各一個cold process。先試G16，若12h速度／5h品質及剩餘時間保留5h的門檻不成立，正式前全局選G8。
- 品質：全30題，P1024，5組配對初代及variation seeds0–4。初代由ASGP原生initializer產生，全部1024個體須admit，沒有替換／篩選。先試G128，縮小規模則G64。每題固定本地原生資料pool、最多200個unique training與1000個input-disjoint test；有限域保留原始train/test支援範圍，必要時確定性保留20%域作測試。
- 所有GAGP使用同一binary、whole AST2048／depth24、每phase1024與16GiB workspace。ASGP logical phase depth7與原生演化規則保留；仍不是相同搜尋空間／RNG。
- 品質test在規模選定與搜尋完成後才作離線判題。每代training champion與final candidate均用各自原生CPU表示核對；不以test挑選candidate。沒有simplification。獨立變異的五個runs只能作探索性品質比較。
- 每次GPU搜尋沿用既有first16 final-program CPU fitness audit；不是全population驗證。每代統計／champion、錯誤、超時、漏評、VRAM/RSS及所有失敗均保留。主要時間為evolve_wall_ms；另列process wall與離線audit。
- 時間試跑只使用資源／耗時選規模。若pilot正好符合正式選定的完整規格，保留為該格唯一一次cold sample；不選最快重複測次。較大pilot在縮小規模時另列，不混入主表。
- 前置成本估計只用舊G4搜尋時間作基準，由真實長程pilot校正；預測值不是結果。NVML每200ms記錄整卡顯存，包含桌面，可能漏短尖峰。
- CPU／GPU工作串行並取得共用GPU鎖；沒有競爭的build／測試／profiling。沒有使用ncu或將CPU繁殖當作GPU主線。

本輪只在benchmark adapter增加明確opt-in的案例數宣告、population seed、原生資料pool、指定資料判題及best-only export。production runtime保持12c6849相同。舊資料／binary均不覆寫。

```bash
PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/day24-20261017/run.py --out logs/day24-20261017
# 已有目錄只允許在確認舊controller／子程序已退出後恢復：
PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/day24-20261017/run.py --out logs/day24-20261017 --resume
```

原始資料、pinned binary、manifest與進度位於logs/day24-20261017。中斷／失敗原樣保留；resume不會把失敗格覆寫成重跑樣本。尚未完成搜尋不得宣稱整輪完成。

本輪另設暫時性T3進度檢查，每30分鐘回到原對話核對controller、子程序及manifest，正常執行時不啟動競爭的工作。首次檢查2026-10-17 14:51:05 UTC；task ID、原始prompt及cadence存於`logs/day24-20261017-monitor.json`。搜尋、離線核對及報告完成後，已將monitor停用。這個檢查不重設24h期限，也不重複啟動活著的controller。

分析程式在正式held-out判題前凍結；完成後才產生新的報告目錄：

```bash
PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/day24-20261017/report.py \
  --matrix logs/day24-20261017 --out <NEW_REPORT_DIRECTORY>
```

報告以已選定的G過濾pilot，保留150個速度格與300個品質runs，另外列出coverage、失敗與初始已成功的runs。每代曲線使用原生CPU判題的exact accuracy；不是只看搜尋fitness。所有比率按本輪原始秒數計算。

最終核對時修正一處pool/support文案：欄位`heldout_outside_training_support`實際表示未出現在原始training pool，不保證是分布外推。另明確標示gen0 champion並補外部ASGP source provenance；沒有改變數值、指標、成功分類或搜尋設定。初版保存在`logs/day24-20261017-report-initial`，修正前後JSON／CSV／PNG雜湊相同，詳見[analysis-corrections.json](final/analysis-corrections.json)。

Post-run完整性檢查不啟動benchmark；本輪已核對591個檔案雜湊、資料配對、五組不同初代、正式命令與全部串行程序區間：

```bash
PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/day24-20261017/verify.py \
  --matrix logs/day24-20261017 \
  --report benchmarks/fixed_asgp/optimization/results/day24-20261017/final \
  --out <NEW_INTEGRITY_JSON>
```
