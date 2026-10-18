# 第1至第3項結果

全部指定搜尋與train/test稽核完成：900/900個搜尋完成train/test CPU判題；9題、P1024、G64、20組新配對seeds。

- ASGP 1T：final聯合成功41/180；gen0 champion已成功0，後續final新增41，初代成功但final失去0；完整判題180/180。
- GPU baseline：final聯合成功39/180；gen0 champion已成功0，後續final新增39，初代成功但final失去0；完整判題180/180。
- GPU execution：final聯合成功39/180；gen0 champion已成功0，後續final新增39，初代成功但final失去0；完整判題180/180。
- GPU operator：final聯合成功39/180；gen0 champion已成功0，後續final新增39，初代成功但final失去0；完整判題180/180。
- GPU + CPU guard：final聯合成功39/180；gen0 champion已成功0，後續final新增39，初代成功但final失去0；完整判題180/180。

相對execution-only，新算子final聯合成功由39/180變為39/180；以計畫seed的成功數作描述性逐題比較，3題增加、2題減少，其餘相同。主要9題配對成功檢定經Holm後，0題支持提升、0題支持退步（p<0.05）；完整paired樣本數及區間見CSV。沒有顯著差異不代表等價。 所選GPU政策與ASGP的final聯合成功分別為39/180與41/180；此跨題加總僅描述本輪cohort，品質差距仍須逐題與不確定性解讀。

本輪有明確的GPU執行加速證據；新算子及CPU複核未證明提高heldout解題率。受檢範圍未找到CPU/GPU評分差異，但品質落差仍未被唯一歸因；保留演算法與執行優化的分開比較。

新資料execution-only相對原GPU：is_sorted 9.59×（逐題配對中位數bootstrap95%：8.89–9.87×）；all_equal 9.94×（逐題配對中位數bootstrap95%：9.36–10.27×）；各題20配對seeds。 CPU複核19584個program觀測，fitness／wrong修正皆0/0，CPUwall 80.644s、占guard搜尋9.00%。正式guard的搜尋wall相對同task/seed operator，中位增加9.71%；這與CPU複核占搜尋wall的比率分母不同。離線雙split CPU判題另耗程序wall合計1155.238s，不含於上述search_s，也不從搜尋時間扣除。

完成同候選／算子診斷、預設關閉且明列成本的CPU訓練校驗，以及TupleSummary GPU執行優化。新算子選擇為site7，只依舊training選出。詳細結果及每題差異見[完整報告](REPORT.zh-TW.md)。

限制：品質範圍9選定題；fixed-work speed不等於time-to-solution；部分CPU校驗不保證全population；native grammar-site政策不普遍等於ASGP source邏輯成本；未完成stock ASGP論文完整重現。
