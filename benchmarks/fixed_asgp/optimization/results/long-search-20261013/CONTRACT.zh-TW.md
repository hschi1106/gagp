# 長世代比較契約

- 五模式 ASGP 1T / GAGP CPU / GPU-e / GPU-er / GPU-er-o；P=1024、完整1024 cases，CPU1T，GPU串行，無profiler。
- 主矩陣 House64、Median128 generations，seeds0/1/2；同一份frozen初始族群。Sum初始已有解，不重跑品質實驗。
- 主矩陣完成後，按事先固定的seed／mode順序，若剩餘時間至少為對應128代run時間的3倍加10秒，重跑Median256。延長分配只看時間，不看fitness；不同run前綴需核對。重跑成本全部計入實驗budget。
- performance runner總budget3300秒（55分鐘），含匯入／最終audit／history export等process開銷。超限僅停止本實驗自己建立的process group；未完成run保留trace但不得偽裝完整結果。獨立checkpoint audit與出圖另外記錄時間。
- 使用上一輪封存binary，不重build／改runtime／改grammar／改operator／改RNG／prepare原輸入；記錄binary及frozen inputs SHA256、環境設定、GPU狀態。
- fitness：House capped absolute error（每case cap1000）；Median admitted Int output使用cap1，對齊ASGP exact mismatch。ASGP使用原生評分，不能以翻譯至GAGP的分數代替。
- Generation包含完整評估、selection、variation與下一代準備；search wall另含冷啟動與final evaluation。process wall再含匯入／audit／export。不同口徑分開。
- 主fitness圖以每個seed一欄，每題一列；五模式的training best-so-far，不平滑、不以seed中位數掩蓋個別停滯。generation0是初始族群的第一次評估；末端額外final evaluation單獨標示。
- 保留best、mean、best_nodes、時間及可用error／timeout／fallback等原始欄位。GAGP既有binary沒有逐代population mean_nodes，不以champion nodes冒充；只能另報final population mean_nodes。
- 在16/32/64/128/256個完整generations結束點（適用者）及終點，依training選當時最佳champion，平手取最早。以獨立1024 held-out cases重評，held-out不參與選擇。GAGP重新grammar admission與CPU/GPU完整重評，ASGP原生重評。
- 品質主圖與同時間圖保留所有seed；同時間比較不得外推已結束run，完整各自時間範圍另列。只有三個reproduction seeds，初始population不變，不能推論收斂等價。

重現：

```bash
python3 benchmarks/fixed_asgp/optimization/long_search.py --out logs/optimization/long-search-NEW --budget-seconds 3300
```

輸出目錄必須是新路徑；此次raw archive為`logs/optimization/long-search-20261013/`。
