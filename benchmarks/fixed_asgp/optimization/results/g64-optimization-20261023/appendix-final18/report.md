# G64追加開發結果

**同代精確分組未增加整體速度，未採用。Packed-frame建置逾時，尚無性能結論。**

原正式結果仍為1.079257×，1.3×目標未達。本附錄使用舊training，P1024／G64／seed0／每格一次，沒有替換正式資料或加入新heldout。

| 對照 | 全30題搜尋wall幾何平均倍率 |
|---|---:|
| 本輪新測選定控制 ÷ 同代分組 | 0.986011× |
| 同一binary關閉分組 ÷ 開啟分組 | 0.971336× |
| 本輪新測beb5585d原基準 ÷ 同代分組 | 1.042952× |

倍率大於1表示分組較快。這是一次舊training計時，沒有將小幅差異當成穩定收益，也沒有使用歷史分母。額外2%門檻未過，seeds1–2及65冠軍CPU確認未執行。

## 做了什麼

- Packed-frame在隔離來源整合40／48-byte AoS／SoA儲存、host配置、lane位址、VM initial／replay；沒有降低精度或容量。觸及既定60分鐘建置上限，0項native、0/150搜尋；150個未執行格保留於[CSV](../development-packed15/runs.csv)。來源／schema失敗13、14及逾時15均保留。
- 同代分組在GPU建hash表，逐欄比對完整genotype／fuel／origins／Value位元。各組直接指向代表，代表本次評分後只複製fitness／wrong／case counters；不重用payload handle，不改RNG、算子、案例或資源限制。已有跨代parent-cache繼續運作。
- 17的隔離Release建置通過；六項native實際6/6通過（458.60秒）。四臂120/120搜尋成功，30/30題獨立核對完整配對。功能沒有合併到正式CPP。

## 為何沒有收益

實際physical program-cases由231,858,061降至230,022,601，只減少0.792%。1585/1920個generation出現新增分組，共跳過13,083筆非既有parent-cache的個體評分。

同binary的30題內部分項加總：reproduction由15.479秒增加至17.755秒；evaluation由77.794秒降至77.424秒。新增分組／傳回工作列在原搜尋wall內；分項不是額外的headline或獨立kernel profile。資料支持目前工作節省太少、未抵銷新增開銷；沒有新增nsys分析細分每個kernel的因果成本。

## 正確性與品質證據

- 四臂完整最終1024個AST、最終fitness／wrong描述、64筆冠軍AST／fitness一致。逐代best、best_wrong、best_nodes、mean、errors、timeouts及logical cases也一致。
- 搜尋後native CPU每次抽查16個最終個體，共1920筆fitness／wrong觀測，0不符；CPU audit區段合計7.359934秒，另列。搜尋中CPU verification為0個體、0修正。
- 未逐代重算每個個體，未新增逐代冠軍native CPU judges、heldout或獨立品質種子。未證明5pp非劣性或普遍等價；沒有新的ASGP倍率。沒有從初代／後續解出率推導新結論，因未執行本附錄的新train/test冠軍判題。
- 固定input、captured harness、官方451檔／隔離來源、binary角色及原始輸出hash均重核。搜尋皆於原09:51:28UTC cutoff前完成，硬截止13:51:28UTC不變。抽樣VRAM／完整程序／audit成本保留於[明細](tasks.csv)；整卡取樣包含桌面且可能漏掉短峰。

首次手動分析錯把「分組個體數×logical cases」當成physical節省量，忽略既有shape／case cache。錯誤草稿保留，修正後使用實際counter差值與保守上界；原始資料、搜尋、倍率及資格沒有更動。見[修正紀錄](../ANALYSIS_REVISION18.json)。新增5項分析guard實際通過；這不等於新候選通過完整136項原生回歸。

[互動頁](dashboard.html) · [PNG](development.png) · [PDF](development.pdf) · [逐題CSV](tasks.csv) · [搜尋資格](../development-groups17/report.md) · [完整audit](../development-audit18/results.json)
