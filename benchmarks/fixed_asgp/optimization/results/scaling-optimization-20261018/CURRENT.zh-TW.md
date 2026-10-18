# 目前做到哪裡（進行中，非最終報告）

更新：2026-10-18 13:06 UTC。本輪截止台灣10/19 19:18，不延長。

本輪要確認「GPU優化放大到16,384個體後是否仍正確、能否再加速」，再用全30題的新ASGP控制組比較。既有tuple execution-only優化的大population pilot已完成；新工作分組量測入口已編譯、通過整合測試。先測的較小分組沒有整體收益，保留原512，正在另外測更大的1024，尚未固定正式候選。

| 項目 | 目前結果 |
| --- | --- |
| Pilot覆蓋 | 9/9筆完整完成、0失敗、0未執行；三題各三方法 |
| Population（每代個體數） | 16,384 |
| Cases（每個個體評分的輸入數） | 1,024 |
| Generations（演化代數） | 8，再做最後一批評分 |
| 已完成的題目／方法 | Is Sorted／All Equal／House Robber的ASGP、原GPU及tuple execution-only GPU全部完成 |
| GPU搜尋時間 | 原GPU175.618秒，tuple execution-only34.091秒，原GPU／優化GPU＝5.151× |
| 正確性核對 | 六筆GPU搜尋各16,384個最終個體，共98,304次CPU fitness與錯誤案例數核對均0差異；三題的原／優化GPU最終descriptors、各代champion fitness及完整最終AST均一致 |
| 顯存 | 整卡約200ms取樣峰值21.116GiB，在24GiB RTX3090完成；含桌面、可能漏掉短尖峰 |
| GPU繁殖 | 六筆皆native-gpu-phase-v2-counted，沒有CPU繁殖替代 |
| 相對ASGP的倍率 | Is Sorted：同輪ASGP404.975秒，原GPU175.618秒（2.306×），優化GPU34.091秒（11.879×） |

三題pilot的完整搜尋wall如下，均為P16384／1024cases／G8／seed0、同輪新量測：

| 題目 | ASGP秒 | 原GPU秒 | 優化GPU秒 | ASGP／優化GPU | 原GPU／優化GPU |
| --- | ---: | ---: | ---: | ---: | ---: |
| Is Sorted | 404.975 | 175.618 | 34.091 | 11.879× | 5.151× |
| All Equal | 359.655 | 173.736 | 32.552 | 11.048× | 5.337× |
| House Robber | 121.918 | 6.500 | 6.584 | 18.519× | 0.987× |

House Robber的tuple開關不適用其評分路徑，本次約1.3%變慢也保留；不能說三題皆獲得新加速。這些是單seed pilot，正式全30題表尚未開始。

搜尋時間包含演化初始化、各代評分／GPU繁殖及最後評分；本輪額外CPU核對與程式匯出在搜尋計時之外，仍列入24小時總預算。兩筆程序總時間分別957.018／818.666秒，不能把搜尋的5.151×套用到含完整離線核對的總程序。第一筆CPU核對本身772.611秒。

這個結果表示一題一seed在指定設定完整執行與核對通過，不表示解出所有題目、ASGP與GPU演化分布／搜尋品質相同，或任意輸入的CPU/GPU語意皆等價。11.879×是相同工作規模的搜尋wall倍率，不是達成相同解題成功率的倍率。這裡的5.151×也不能與先前P1024／200cases／G64直接解讀為純population scaling，因為案例數與代數不同。正式表會固定共同案例數／代數，再並列P1024與P16384。

Release benchmark build、harness4/4及adapter CTest1/1（48.48秒）通過；兩筆nsys已完成，profile時間不加入加速比。先前40筆較小分組調校完整結束：256／128／64的相對512幾何平均倍率約0.865／0.695／0.528×。其中256在Is Sorted P16384的最終分數描述有差異，雖完整AST相同、前16個體CPU核對0差異，仍不通過候選的一致性門檻，沒有放寬門檻或重測換結果。

接著在另存20筆development中比較512／1024，要求全部10組題目／P的完整最終AST、descriptors、champion AST／fitness及CPU核對符合門檻，再依固定時間規則凍結全30題正式設定。原40筆負面結果保留。正式分母全部重新量測。

Raw依據：`logs/scaling-optimization-20261018/pilot16384/manifest.json`及其三題／三方法JSON；Is Sorted兩GPU完整最終AST匯出SHA256皆為`806c23623f4026979af18ba41d4caf2d8946618c4c702349af020e835902f204`。Pilot與原40筆development已退出，目前1024分組extension在執行，正式報告尚未產生。
