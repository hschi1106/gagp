## 本輪結論

**新 GPU reproduction 能以速度換取更多搜尋，但不能宣稱多世代品質全面勝過 ASGP，也不能把 frozen-parent 的30×直接套到整趟搜尋。** 90次搜尋 process wall 合計約40.6分鐘；加上設計、build等間隔，性能實驗約50分鐘完成。事後重評與出圖另計。

- **固定16代品質：** Sum 初代就有正確解，五模式均保留 best-so-far，training／held-out皆0。House 的 GPU-er/o seeds 為−7290／−8245／−7574，ASGP為−7376／−7559／−7431，沒有一致優勢。Median（exact）GPU-er/o為−614／−642／−642，ASGP為−642／−642／−631，同樣有seed差異。
- **相同10秒品質：** House 的 GPU-er為−4493／−1751／−4631，ASGP為−7376／−8254／−7574；對應held-out為−4689／−1926／−4829與−7891／−8738／−8394，三個seed都有優勢。GPU-er-o最後一個seed在十秒截止前少完成部分代數，為−4921；同seed時間截止點不保證選到相同代數。Median GPU-er/o為−470／−638／−642，ASGP為−642／−642／−578；改善不穩定，GPU seed1 的小幅training改善也沒有反映在held-out。
- **16代搜尋速度：** 最快GPU的 Sum／House／Median分別3.142／1.279／1.022秒，包含cold與final eval；相對同輪ASGP為9.97×／10.78×／0.76×。Median這個短搜尋仍是ASGP更快。Process包含不完全相同的封存／audit工作，另列，不與Search混用。
- **前後期：** 前16代GPU-er-o的late/early中位數約1.27×／1.01×／0.99×；這不代表長跑穩定。House seed0的512代延長組，前4代中位數35.56ms、末4代409.27ms，相差11.51×。GPU-e同題長跑也明顯變慢。長跑程式大小與成本必須一起看，不能只報前幾代。
- **fitness客製化：** 值得提供，並且應把metric、distance cap、execution-error penalty與secondary ranking分開。這輪只增加benchmark設定、對齊Median的整數exact目標，沒有新增任意callback API，也沒有改reproduction架構。原L1與exact對照並未顯示某個metric對所有seed都更好。

### 必須保留的語意警示

ASGP Median原生分數與其翻譯成GAGP後的分數，有829筆champion紀錄不同（含重複與legacy對照，76個不同翻譯程式）。**正式圖表及表格已使用ASGP原生分數，held-out也重新由ASGP原生執行。** 翻譯後GAGP CPU/GPU一致，不能據此推導ASGP/GAGP執行語意相同。

已確認的一個原因是負索引：ASGP `kIndex`對`at<0`報錯，GAGP `index`依spec採Python負索引。觀察到的seed2 champion在三元素輸入上可化為`xs[trunc(xs[0]/54)]`；對`[-100,20,-50]`，ASGP的−1索引失敗，GAGP取最後一項−50而正確。將這筆輸入重複1024次的獨立診斷，也實測得到ASGP fitness−1024、GAGP CPU/GPU均0（不混入benchmark）。因此不能以翻譯後較好的fitness替ASGP原生成果加分。其他不同程式本輪未逐一分類；沒有趁量測更改既有runtime或frozen inputs。

CPU與GPU-e的16代best曲線一致，但Sum有1個、House有9個generation的mean不同，最大差23.443359375；不能稱為整個population完全parity。既有baseline也記錄過House frozen個體的CPU/GPU差異，本輪未逐一定位所有中間個體。GPU-er/o的16代best／mean則全部一致。所有延長組的前16代記錄與同模式主run一致。

正式品質是「相同task評分公式、各系統各自執行語意」的系統比較，不是跨runtime程式等價證明；也不能把operator、語言、tie-breaking及RNG差異單獨歸因GPU。

### 驗證範圍

記錄的champions共10,330筆，去重後4,109個程式，均完成grammar admission與完整1024 cases的GAGP CPU/GPU重評，0項fitness不一致。ASGP原生training snapshot全部重現其搜尋記錄。每個GAGP run的final top16 CPU audit均0 mismatch。這些檢查不等於每代完整population逐程式CPU/GPU等價。

Native benchmark整合測試通過（含原五模式、五個search入口、Median exact初始分數、snapshot、非法penalty拒絕）；5個fixed-tool及24個repository checks通過。GPU主矩陣timeout／fallback／unscored皆0，Sum及Median仍有約4%的真實runtime-error pairs並已扣分；CPU／ASGP沒有對應counter，不填成0。
