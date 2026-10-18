# 目前做到哪裡（進行中，非最終報告）

更新：2026-10-18 12:32 UTC。本輪截止台灣10/19 19:18，不延長。

本輪要確認「GPU優化放大到16,384個體後是否仍正確、能否再加速」，再用全30題的新ASGP控制組比較。現在先完成既有tuple execution-only優化的大population驗證；新工作分組調校程式已加入，但尚在等待pilot結束後編譯／測試，還沒有新的調校收益。

| 項目 | 目前結果 |
| --- | --- |
| Pilot覆蓋 | 9筆預定，5筆完整完成、0失敗，1筆All Equal優化GPU執行中，3筆House Robber尚未開始 |
| Population（每代個體數） | 16,384 |
| Cases（每個個體評分的輸入數） | 1,024 |
| Generations（演化代數） | 8，再做最後一批評分 |
| 已完成的題目／方法 | Is Sorted三方法全部完成；All Equal的原GPU與ASGP完成，優化GPU全population CPU核對中 |
| GPU搜尋時間 | 原GPU175.618秒，tuple execution-only34.091秒，原GPU／優化GPU＝5.151× |
| 正確性核對 | 兩方法各16,384個最終個體，CPU fitness與錯誤案例數均0差異；完整最終程式JSON的SHA256相同 |
| 顯存 | 整卡約200ms取樣峰值21.116GiB，在24GiB RTX3090完成；含桌面、可能漏掉短尖峰 |
| GPU繁殖 | 兩筆皆native-gpu-phase-v2-counted，沒有CPU繁殖替代 |
| 相對ASGP的倍率 | Is Sorted：同輪ASGP404.975秒，原GPU175.618秒（2.306×），優化GPU34.091秒（11.879×） |

Is Sorted這組三方法已完整結束，GPU兩方法的全最終個體描述、各代champion fitness軌跡與完整最終AST均一致。All Equal目前已得ASGP359.655秒／原GPU173.736秒，優化GPU仍在額外CPU核對，待完整輸出通過再報倍率；不從stderr推算完整搜尋時間。

搜尋時間包含演化初始化、各代評分／GPU繁殖及最後評分；本輪額外CPU核對與程式匯出在搜尋計時之外，仍列入24小時總預算。兩筆程序總時間分別957.018／818.666秒，不能把搜尋的5.151×套用到含完整離線核對的總程序。第一筆CPU核對本身772.611秒。

這個結果表示一題一seed在指定設定完整執行與核對通過，不表示解出所有題目、ASGP與GPU演化分布／搜尋品質相同，或任意輸入的CPU/GPU語意皆等價。11.879×是相同工作規模的搜尋wall倍率，不是達成相同解題成功率的倍率。這裡的5.151×也不能與先前P1024／200cases／G64直接解讀為純population scaling，因為案例數與代數不同。正式表會固定共同案例數／代數，再並列P1024與P16384。

接著依序完成三題pilot、自動build／回歸／nsys／工作分組調校，檢視實際瓶頸後選一個全局候選，最後依固定時間規則凍結全30題正式設定。所有失敗與尚未執行的格子保留，正式分母全部重新量測。

Raw依據：`logs/scaling-optimization-20261018/pilot16384/manifest.json`及其`is_sorted.p16384.{asgp,gpu_base,gpu_exec}.s0.g8.json`。兩GPU的完整最終AST匯出SHA256皆為`806c23623f4026979af18ba41d4caf2d8946618c4c702349af020e835902f204`；pilot程序仍在執行，正式報告尚未產生。
