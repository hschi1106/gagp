# 全30題大population GPU優化（完成）

先看 [易讀結論](CURRENT.zh-TW.md)，完整數據見 [中文報告](final-v3/REPORT.zh-TW.md)、[逐題秒數CSV](final-v3/cells.csv)、[互動表](final-v3/dashboard.html)、[驗證紀錄](final-v3/validation.json)。正式搜尋與CPU冠軍判題各360／360完成，0執行失敗，合格速度格60／60。完整性核對2352檔案、720個串行程序區間均通過。

| P | 合格題數 | ASGP／原GPU幾何平均 | ASGP／優化GPU幾何平均 | 原GPU／優化GPU幾何平均 |
|---:|---:|---:|---:|---:|
| 1,024 | 30/30 | 7.86× | 9.30× | 1.18× |
| 16,384 | 30/30 | 11.10× | 12.43× | 1.12× |

固定窗口2026-10-18 11:18:51 UTC至2026-10-19 11:18:51 UTC（台灣10/19 19:18:51），沒有重設。正式搜尋01:39:03 UTC、冠軍判題02:10:25 UTC結束；報告、實際圖表／瀏覽器與檢查亦在原窗口內完成，時間見validation.json及raw交付紀錄。全部controller／children已退出，勿resume這輪已完成工作。舊day24／quality-control亦不重新啟動。

## 固定設定與可比範圍

- 全30題 × P1024／16384 × ASGP1T／原GPU／tuple execution-only GPU × G8 × 各2次冷程序，共360筆；每個體C1024。ASGP分母全部本輪新量測。
- CPU1T、GPU evaluation＋reproduction、block512、16GiB workspace、physical AST2048／depth24／phase1024；原GPU tuple flag0、優化組1，沒有CPU繁殖替代或CPU training guard。
- 初代取凍結source前P個體；兩GPU共用對應prepared檔。seed0是演化亂數，計時重複不重新抽父代或案例，不能當獨立品質seeds。
- GPU rep0核對ALLP最終個體的CPU fitness＋wrong並保留完整AST；rep1核對前16並匯出已選冠軍。兩GPU120組execution comparisons均一致，其中60組比較完整最終族群。全部final與每代已選冠軍另做CPU共同輸出投影判題，與raw ranking fitness分開。
- 只有固定訓練案例，沒有新增獨立test split；不宣稱泛化品質、ASGP算子分布等價、paper replication或time-to-solution。來源ASGP logical phase depth7與native physical depth24也不是同一結構量尺。

權威設定：[protocol.json](protocol.json)、[時間分配規則](allocation-policy.json)。正式freeze：`logs/scaling-optimization-20261018/formal-freeze-attempt1.json`，SHA256 `4a3cb7b9fd8ac9838ab2c791ad235fe5e3b89437cb7ba4116ed4d54459456276`；同一binary SHA256 `731697e685a3ca854027a83f18b87dd0e96e41532363cd3186d01c6f5a527ade`。完整source／external ASGP directory／linked inputs／harness／input／output hashes在 [audit.json](final-v3/audit.json)。

## 實作、負結果與驗證

新增benchmark blocksize量測入口及匹配CPU核對lanes；較小分組整體變慢，1024分組平均快約6.4%但一格完整程式不同，依原gate淘汰。compact-root小陣列VM原型已實作與測試，20組完整配對一致，但平均僅1.008875×、未達1.03×，已封存移除。正式採用既有tuple路徑，不宣稱本輪新VM收益。原型來源／patch／失敗皆保留在raw。Nsys兩個probe的VM占kernel時間97.9／97.5%，profiling時間未混入倍率。

固定父代診斷30／30程序完成：336個明列phase-root替換中296匹配、40無法映射（五個動態規劃題各8個）。只涵蓋前8父代／4組配對，不涵蓋internal sites、mutation generator或RNG分布；既知四題HeadIndex site-count差異另列，不推定同因。

最終prototype移除後的確切binary完整Release build與native131／131通過255.09秒；後續study報告／判題57項、repository24項通過；staged格式核對保留標準CSV CRLF與四個凍結檔的EOF空行，使用明列cr-at-eol／-blank-at-eof規則通過，預設診斷與政策見 [format-check.json](final-v3/format-check.json)。這些是不同實際檢查，沒有宣稱新完整tools131。兩張PNG已檢視；最終T3桌面728／手機360各56互動檢查通過，另檢視手機篩選／test提示／頁尾；html_render使用未插入檢查腳本的原始最終HTML。

## 報告版本、原始attempts與重現

唯一交付版本是 `final-v3/`。首版HTML超過T3工具512000字元限制，第二版縮減顯示資料後又由實際截圖修正低對比配色；原兩版已完整封存於 `logs/scaling-optimization-20261018/report-presentation-attempt0/` 與 `report-presentation-attempt1/`。三版科學summary（除生成時間）、全部CSV與PNG雜湊相同，沒有重測搜尋或更換候選。位置與hash對照見 [presentation-history.json](final-v3/presentation-history.json)。

所有scientific／harness失敗、二進位與sample attempt保留於 `logs/scaling-optimization-20261018/`。原formal runner在任何正式樣本前同鎖自鎖，原空stage／freeze／錯誤保留；新formal_attempt1只修正controller／stage鎖名與路徑，binary／科學設定不變。詳細歷史與已完成狀態見 [RECOVERY.md](RECOVERY.md)。

報告可用 `PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/scaling-optimization-20261018/report.py --out <全新輸出目錄>` 在實際worktree重建；預設讀本輪formal-attempt1及champion-judges-attempt0。report.py取得共用GPU鎖並拒絕active controllers／children及既有輸出目錄。勿重跑已完成matrix或覆寫任何結果。

所有命令worktree為 `/home/hschi1106/.t3/worktrees/gagp/gagp-support-20261015`、branch `opt/list-output-5x-20261015`；較舊UI checkout未改動。結果只本地提交、未push，暫時monitor於最終交付停用；raw交付receipt記錄實際commit與停用結果。
