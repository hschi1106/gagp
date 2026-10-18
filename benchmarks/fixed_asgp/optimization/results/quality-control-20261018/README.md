# 品質控制與長程 GPU 改善（2026-10-18，已完成）

已完成使用者授權的第1–3項工作。正式9題 × 20組新配對 seeds × 5方法，共900次搜尋及1800個 CPU train/test 判題工作，全部完成、0程序失敗／未執行。搜尋於2026-10-18 08:35:23 UTC結束，判題於09:03:29 UTC結束；自原起點至判題結束7.762h。報告、檢查及本地提交亦在原24h內，最終時間見 [completion](final/completion.json) 與 [review-validation](final/review-validation.json)。

- [主要結論](final/FINDINGS.zh-TW.md)、[完整中文報告](final/REPORT.zh-TW.md)、[互動頁](final/dashboard.html)
- [逐run CSV](final/runs.csv)、[逐題摘要](final/task_summary.csv)、[配對統計](final/paired_tests.csv)、[逐代CPU曲線](final/curves.csv)
- [速度與品質 PNG](final/speed-quality.png)／[PDF](final/speed-quality.pdf)、[行為多樣性 PNG](final/diagnostic-diversity.png)／[PDF](final/diagnostic-diversity.pdf)、[CPU成本 PNG](final/verification-cost.png)／[PDF](final/verification-cost.pdf)

同候選診斷沒有找到受檢範圍的評分差異：全30題初代30,720候選、中後代144快照147,456候選觀測、ASGP演化後27快照27,648個成功轉譯候選，其fitness／wrong核對均0差異；正式720次GPU搜尋的final前16共11,520候選觀測亦0差異。候選可能跨代重複，這不是任意程式或全population的語意證明，也未把品質差距唯一歸因。

CPU訓練複核已實作為預設關閉的 top-K／interval／near-zero 控制。正式top8/i8受檢19,584個program觀測、3,916,800個program-case，fitness與wrong修正皆0；CPUwall 80.644s，占guard搜尋wall 9.00%。相對同task/seed operator，guard搜尋wall中位增加9.71%；離線train/test判題程序wall合計1,155.238s另計。沒有看到品質收益，應定位為校驗／診斷機制。

TupleSummary執行優化在新資料20組配對seeds的G64未profile搜尋，相對原GPU：Is Sorted中位9.59×（bootstrap95% 8.89–9.87×），All Equal 9.94×（9.36–10.27×）；對ASGP各為6.30×／4.28×。180組execution-only／baseline的所有已匯出champion AST、champion fitness及最終score/wrong/size arrays一致，非全population AST證明。`nsys`只作瓶頸診斷，不混入倍率。

品質未證明整體改善：ASGP final train/test聯合成功41/180，原GPU、execution-only、site7及site7+guard皆39/180；全部gen0 champion成功數0，成功皆在後續final出現。site7相對execution-only，Count Odds 9→14、Last Index of Zero 2→5、Cumulative Sum 13→16，但All Equal 4→0、Double Letters 10→3（每題20seeds）；主要9題McNemar經Holm後無支持提升／退步的題目，不能因此宣稱等價。執行優化有支持證據；算子與CPUguard保留為opt-in，不能包裝成品質提升。

時間窗固定2026-10-18 01:17:45 UTC至2026-10-19 01:17:45 UTC（台灣10/19 09:17:45），從未重設。[protocol](protocol.json)、[分析計畫](analysis-plan.json)、[統計計畫](report-analysis-plan.json) 均保留。CPU1T、GPU評分與繁殖、16GiB workspace、physical AST2048/depth24/phase1024；P1024／G64，各題200 unique train／1000 unique test。每題所有方法及seeds共用相同且不相交的train/test；test亦排除舊quality及完整speed已用inputs，見 [dataset_splits](final/dataset_splits.csv)。正式全卡NVML約200ms取樣最高18.950GiB，含桌面且可能漏短尖峰。

自動選擇在2026-10-18 04:22:53 UTC凍結：[candidate-selection](candidate-selection.json) 僅用舊training選出site7（tuple＋uniform phase sites＋source crossover gate＋grammar-site depth7，parsimony關閉）；[validation-freeze](validation-freeze.json) 按固定時間規則拒絕20×G128的24.685h估計，選20×G64，含緩衝／準備／報告預估14.592h。其後才建立資料seed20261018與初代seeds5–24的正式驗證。沒有按新test改設定、替換不良初代或隱藏重測。

同一pinned binary SHA256：`d347d95617aba1676af11b05d786bf06db882d9eddff6b97756207b3d8d9a48a`；核心本地提交4ca0909，正式harness起於e780809。原始資料、不可變binaries、完整exports／判題及各次attempt保留在 `logs/quality-control-20261018/`。[完整性核對](final/integrity.json)涵蓋5160個檔案雜湊、runtime／harness／外部ASGP46來源、切分與初代、成功輸出；全部正式程序時間區間串行且在原deadline內。

實際驗證：Release全build PASS；完整native初次130/131，唯一frozen-spec文件hash gate失敗已修正，gate＋adapter重跑2/2，最後raw-score／behavior scope修改再以pipeline＋adapter2/2通過。tools122（4skip）、repository24；最終分析／驗證工具15及repository24通過。沒有宣稱單次fresh131全通過。三張實際圖已檢視，T3 html_preview於728／360各71項互動檢查通過，含篩選、9題選擇、train/test、觸控、提示值重設及無橫向溢出，見 [UI核對](final/ui-validation.json)。

報告整理曾因重複task欄位失敗，保留attempt0；完整首版保留在`logs/quality-control-20261018/report-initial/`。後續加入離線判題耗時欄位及解讀、修正手機觸控與提示值；既有科學CSV數值、PNG與推論JSON不變。詳見 [報告修訂紀錄](report-corrections.json)；實驗harness、binary、設定及樣本皆未改動。

品質僅9個選定題／單一新資料切分／20seeds；fixed-work倍率不等於time-to-solution。4題source HeadIndex的native lowering使site counts不同，不能宣稱搜尋空間等價、全30題品質重現、stock ASGP論文重現或所有population parity。

本輪控制程序與children皆已退出；不要resume已完成矩陣。需要重新產生報告時使用NEW目的目錄：`PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/quality-control-20261018/report.py --out <NEW>`。所有timed jobs／build／heavy analysis共用`/tmp/gagp-support-20261015-gpu.lock`。暫時monitor已停用，舊day24 monitor仍停用，見 [停用紀錄](final/monitor-disabled.json)。所有變更僅在本地，沒有push。
