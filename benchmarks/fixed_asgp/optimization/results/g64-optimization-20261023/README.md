# GAGP：64代加速實驗

**新GAGP比本輪開始時最快版本快1.079×，相對本輪ASGP1T為4.673×。未達1.3×目標。**

先看[報告](final/report.md)的數字與限制，再用[互動圖表](final/dashboard.html)查看個別題目。[逐題表](final/tasks.csv)、[P16384表](final/secondary_tasks.csv)及[品質配對](final/quality_pairs.csv)保留明細。最終報告已獨立重核原始輸出、AST mapping與來源／binary／input hashes。

| 比較內容 | Population（每代個體數） | Generations（代數） | 題目／重複 |
|---|---:|---:|---|
| 原GAGP、新GAGP、ASGP1T速度 | 1024 | 64 | 30題×3輪，初代seed35 |
| 原／新GAGP演化品質 | 1024 | 64 | 30題×10個seeds（35–44） |
| 三方法的大族群速度 | 16384 | 64 | 30題×1輪，獨立列示 |

倍率＝對照方法搜尋時間÷新GAGP搜尋時間；大於1表示新GAGP較快。P1024三輪配對時間95%區間為1.077–1.081×，只描述固定任務／初代的計時重複。P16384相對舊GAGP為1.080×、相對本輪ASGP1T為8.076×，每題僅一輪。兩個P各30/30題通過指定原始證據核對，全部960次正式搜尋與3240個離線判題工作完成，無正式執行失敗。

品質300組新舊配對的逐代冠軍AST／fitness、最終分數描述與最終匯出一致。兩版各90/300次執行最終train+test全對，其中21次初代已解、69次演化後才解出；這是「題目×seed」次數，不是90個不同題目。10個seeds不足以證明5pp非劣性。ASGP的seed35品質另外列示，不混入這300組。

收益集中：Tribonacci 3.606×、StockCooldown 1.555×、FibonacciArray 1.510×、IsSorted 1.358×、AllEqual 1.340×。CountOdds 0.820×、ScrabbleScore 0.883×、FuelCost 0.934×反而變慢；因此整體只有1.079×。

## 採用的改動

- 私有TupleSummary評分把有效案例集中排進GPU tiles，分數寫回原位置並依原順序加總。
- View kernel依案例數與已編譯運算特徵選擇執行緒數，保留logical lanes、評分與演化規則。

沒有減少案例、代數或改用CPU繁殖。後續布局特化、記憶體交錯、小型堆疊等嘗試未取得額外收益，未採用。完整範圍與負結果見[開發嘗試表](development_attempts.md)；4題舊training的nsys觀察另見[瓶頸紀錄](development_profile.md)，不當作全30題加速比。

## 比較範圍

- 每個實驗格的三方法共用案例與初代。資料seed20261024；每題train/test互斥，跨演化seed使用相同split。Training每題最多200 cases，test最多1000；有限輸入域較少。
- CPU Intel(R) Core(TM) i9-10900K CPU @ 3.70GHz（host worker1T）；RTX 3090（24GiB）執行GPU評分及繁殖；16GiB workspace；GAGP native grammar為AST2048／depth24，GPU reproduction容量1024。同一候選binary與全域設定貫穿正式矩陣。
- 品質核對全部65個冠軍觀測的train/test；速度首輪另做完整最終族群CPU稽核。未逐代重算每個個體；10個seeds不足以證明5pp非劣性或普遍等價。
- 兩個GAGP版本共用搜尋wall計時邊界。ASGP包含parents複製、grammar建立及每代history編碼；GAGP的compiled grammar準備、JSON匯出與CPU16稽核位於搜尋外。完整CLI與驗證成本另列，搜尋中CPU核對費用若有發生，仍包含在搜尋wall。
- ASGP來源phase深度7，表示法、算子與grammar sites未全面對齊GAGP。ASGP倍率是本輪native-engine對照；不是論文重現或相同演化能力的證明。ASGP品質seed35單獨呈現，不混入GAGP的10-seed品質表。
- 失敗、不准入、未知與預算未執行均保留；不重抽或替換樣本。VRAM是約200ms取樣的整卡用量，包含桌面且可能漏掉短暫峰值；CSV保留取樣次數與錯誤，無有效取樣保留未知。

固定窗口：2026-10-23 13:51:28至2026-10-24 13:51:28 UTC（台灣10/24 21:51:28截止），包含驗證與報告。正式設定／N10規模在新資料生成前凍結，不由test結果選擇。

[實驗規格](protocol.json) · [品質規模政策](quality-scale-policy.json) · [目前程序與恢復步驟](RECOVERY.md) · [固定候選執行方式](USE_CANDIDATE.md)

正式版本的完整Release建置通過；正式原生回歸136/136、研究檢查129、工具122（跳過4）、repository24均通過。實際PNG及PDF已檢視；互動頁728／360寬度的篩選、選題、train/test切換與無水平溢位檢查通過，見[UI紀錄](UI_VALIDATION.json)。

[候選preset](candidate_preset.json)已匯出；P1024／P16384操作範例的實際dry-run通過，沒有啟動新搜尋，見[檢查紀錄](PRESET_CHECK.json)。[隔離frame原型](prototype-private-frames/README.md)的儲存／存取檢查與CUDA編譯檢查通過，但未整合、未執行VM或量測加速，不列入正式結果。較早研究與presentations不修改；本輪僅本機提交，不推送。

追加開發已結束：同代精確分組120/120搜尋及6/6定點native完成，但相對本輪選定控制只有0.986×，相對同binary關閉為0.971×，未採用。Packed-frame建置60分鐘逾時，0/150搜尋；沒有性能結論。見[追加報告](appendix-final18/report.md)、[逐題CSV](appendix-final18/tasks.csv)、[PNG](appendix-final18/development.png)／[PDF](appendix-final18/development.pdf)與[實作及限制](PACKED_FOLLOWUP.md)。正式1.079×及1.3未達不變。
