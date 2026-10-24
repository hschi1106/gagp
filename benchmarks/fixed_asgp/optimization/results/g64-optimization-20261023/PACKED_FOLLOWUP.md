# 追加開發：packed frame與同代分組

**同代分組沒有取得額外收益，未採用；packed-frame建置未完成。** 原正式成績仍為1.079×，未達1.3×，原來源、binary、設定與正式結果均不變。

先看[追加報告](appendix-final18/report.md)、[逐題CSV](appendix-final18/tasks.csv)及[互動頁](appendix-final18/dashboard.html)。圖可下載[PNG](appendix-final18/development.png)／[PDF](appendix-final18/development.pdf)。本附錄是舊training、P1024／G64／seed0、每格一次，沒有新增heldout或ASGP分母。

| 方向 | 實際完成 | 結果 |
|---|---|---|
| Packed-frame AoS／SoA | 來源整合；60分鐘建置逾時 | 0項native、0/150搜尋；全部未執行格保留 |
| 同代精確genotype分組 | Release及6/6定點native；120/120搜尋，30/30題四臂核對 | 相對本輪選定控制0.986×；相對同一binary關閉功能0.971× |

倍率＝對照搜尋時間÷新功能搜尋時間；大於1才是加速。同代分組未過原定額外2%門檻，seeds1–2及65冠軍CPU確認未執行；沒有改用其他題目或種子重試。

## 實作

Packed-frame把64-bit payload與tag／bool metadata分開，frame由72／88 bytes改為40／48 bytes；沒有降低精度、frames或fuel。host按Storage大小配置，initial／replay共用AoS或SoA lane位址；root／child／direct leaf按既有state0初始化metadata。只在既有private-window准入後使用，其餘走原路徑。完整[patch](packed_frames_integration15.patch)及[來源紀錄](PACKED_IMPLEMENTATION15.json)保留11個新增／變更檔。454檔隔離來源沒有取代官方451檔。Schema錯誤13／14及逾時15保留；不能由建置逾時推論正確或加速。

同代分組在GPU建立hash表，仍逐欄比較完整genotype、fuel、origins與Value位元。各組直接指向代表，代表完成評分後才複製fitness、wrong與case counters，不複製暫存payload handle，不改RNG或演化算子。跨代parent-cache繼續運作；新增分組及傳回成本包含於搜尋wall。四檔[patch](current_groups_integration17.patch)與[來源紀錄](CURRENT_GROUPS_IMPLEMENTATION17.json)來自獨立官方來源，沒有packed依賴。

## 證據與限制

- 四臂完整最終1024個AST、64筆冠軍AST／fitness、最終fitness／wrong及逐代9項scalar紀錄一致。120次搜尋各native CPU抽查16個最終個體，共1920筆，0fitness／wrong不符。搜尋中CPU核對0個體／0修正；搜尋後CPU audit區段共7.360秒，另列。
- 實際physical program-cases只減少0.792%；同binary reproduction分項30題加總15.479→17.755秒，evaluation77.794→77.424秒。工作節省未抵銷新增成本。一次計時不提供穩定小幅收益或逐kernel因果證明。
- 沒有逐代全族群CPU核對、新65冠軍native judges、heldout或獨立品質seeds；未證明普遍等價、5pp非劣性或1.3×達標。沒有本附錄的新train/test成功率或初代／後續成功率結論。
- Source、captured harness、binary角色、input與輸出hash已重核。首次手動counter統計忽略shape／case cache的錯誤已透明修正，原稿與[修正紀錄](ANALYSIS_REVISION18.json)保留。新5項分析guards實際通過；新候選只跑6項定點native，不能借用原正式136/136來宣稱完整新回歸。

[完整搜尋資格](development-groups17/report.md) · [正確性及成本audit](development-audit18/results.json) · [Packed未執行紀錄](development-packed15/report.md) · [實際UI檢查](APPENDIX_UI_VALIDATION18.json)

所有計時已在原09:51:28UTC cutoff前結束；包含報告的硬截止2026-10-24 13:51:28UTC不變。僅本機提交；不推送、不修改舊研究或presentations。
