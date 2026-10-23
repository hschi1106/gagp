# GAGP 開發對照

**範圍：舊 TRAINING、G8、每方法一次、seed0、CPU16 抽查。**
本頁不提供正式重複量測、完整族群 CPU 核對或獨立品質的不劣性結論；2× 目標尚未由本頁驗證。

| 組合 | 合格／預期格 | P1024 | P16384 | 合併 |
|---|---:|---:|---:|---:|
| stage1/pool | 60/60 | 0.996× | 1.365× | 1.166× |
| stage1/pool_full128 | 60/60 | 1.009× | 1.564× | 1.256× |
| stage1/pool_shape | 60/60 | 1.095× | 1.609× | 1.327× |
| stage1/stage | 60/60 | 1.062× | 1.379× | 1.210× |

只有全部題目、兩種 P 都完成且配對一致的組合才列合併幾何平均；失敗與缺格保留。
分母使用本輪重新量測的上一個最快 GAGP。各組合只使用自身配對控制，不混用控制秒數。

圖表與預設篩選組合：stage1/pool_shape。

搜尋 wall time包含演化函式內的 admission、GPU 初始化、逐代演化與最終族群還原。輸入／結果 JSON、AST 寫出與額外 CPU 核對另計，不能由這個倍率推論總工作時間或 time-to-solution。
GPU 評分及繁殖、16 GiB workspace、CPU1T；來源、binary、輸入與實際讀取的證據雜湊見 summary.json。

資料：[逐格 CSV](development_cells.csv)、[各組合摘要](development_recipes.csv)、[互動頁](dashboard.html)、[PNG](development_speed.png)、[PDF](development_speed.pdf)。
