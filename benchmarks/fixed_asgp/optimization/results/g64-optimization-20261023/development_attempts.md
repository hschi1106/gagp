# G64開發期嘗試

這是舊training上的選擇依據，不是正式新資料結果。所有列為P1024／G64，倍率皆為搜尋wall。

最後採用設定在完整30題、3個舊seeds的90組配對確認為 **1.0621×**，未達1.3×。seed0沿用先前的選定樣本，不能當作額外重複。

| 範圍 | 嘗試 | 相對本輪原GAGP | 相對同階段控制組 |
|---|---|---:|---:|
|30題／1seed|固定128 threads的view設定|1.0301×|1.0015×|
|30題／1seed|加上private launch bound32的控制組|1.0285×|—|
|30題／1seed|案例密集tiles＋hybrid view block（採用）|1.0593×|1.0299×|
|30題／1seed|private layout編譯特化|1.0281×|0.9996×|
|30題／1seed|hybrid加private layout編譯特化|1.0582×|1.0289×|
|8題／1seed|已選hybrid設定（本階段控制組）|1.1704×|—|
|8題／1seed|private cells改SoA排列|1.0134×|0.8659×|
|8題／1seed|精確短list edit-distance路徑|1.1743×|1.0033×|
|8題／1seed|SoA加精確短list評分|1.0154×|0.8676×|
|8題／1seed|SoA／短list加全面dense tiles|0.9731×|0.8315×|
|8題／1seed|已選hybrid設定（本階段控制組）|1.1693×|—|
|8題／1seed|小型register phase stack|1.1503×|0.9838×|
|8題／1seed|register stack加128-thread編譯bound|1.1548×|0.9876×|
|8題／1seed|register stack加simple phase executor|1.1394×|0.9744×|
|8題／1seed|register stack並關閉private measure cache|1.1412×|0.9759×|

30題的hybrid設定保留；編譯特化沒有增加收益。SoA、短list與register stack僅在8題篩選，未取得額外2%收益，沒有進入完整30題確認，也不能把其中約1.17×外推成整體結果。

同階段控制組：private7使用control32；layout8與bank9使用ordinary（當時已選的hybrid設定）。每一階段都重新跑原始GAGP對照，不跨階段混用時間。

表格核對了封閉task集合、成功狀態、P/G/seed、manifest搜尋時間比及保存的最終AST／分數描述／冠軍相等旗標；這不是重跑測試，也不代表每代全族群都做過CPU核對。來源SHA與精確數字見[證據](development_attempts_evidence.json)及[CSV](development_attempts.csv)。

編譯失敗、第一版漏case失敗及未執行記錄仍保留於原始logs與RECOVERY歷史；本表只整理最後一次完整選擇與後續被拒絕的設定篩選，不是全部嘗試清單。
