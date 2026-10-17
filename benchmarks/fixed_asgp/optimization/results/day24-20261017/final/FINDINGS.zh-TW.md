# 全30題實驗結論

本輪完整完成速度150個配對格（300個方法runs）、品質300個runs與600個train/test原生CPU判題程序，沒有程序失敗、超時或未執行格。正式設定為速度G8＋final、品質P1024/G64＋final；品質每個run評估66560個個體。控制器從準備到核對共9.234小時；包含報告與人工檢查的時間另存completion.json，均在原24小時期限內。

速度改善已能在全表觀察到；同時，本輪品質結果顯示目前GAGP仍有落差，不能把搜尋時間倍率當作解題加速比。

## 速度與24GiB可執行性

| Population | 完成題數 | 快於ASGP | 倍率中位數 | 幾何平均 |
| --- | ---: | ---: | ---: | ---: |
| 1024 | 30/30 | 30/30 | 10.64× | 7.95× |
| 2048 | 30/30 | 30/30 | 13.95× | 10.22× |
| 4096 | 30/30 | 30/30 | 16.05× | 11.49× |
| 8192 | 30/30 | 28/30 | 14.94× | 10.30× |
| 16384 | 30/30 | 29/30 | 15.22× | 11.07× |

150格中147格較快。較慢的三格是Small or Large/P8192（0.986×）、Grade/P8192（0.742×）與Grade/P16384（0.984×）；接近1的單次差異不能視為精密排序。P4096之後的倍率並未隨population單調提高。

所有P使用同一binary、完整1024cases、CPU1T、GPU評分及繁殖、16GiB workspace、whole AST2048/depth24/phase1024。P16384全30題成功，沒有降workspace、替換初代或改用CPU繁殖。正式速度run整卡200ms NVML取樣峰值21.114GiB；包含較大pilot等工作時為21.132GiB。這是實際取樣支援證據，含桌面用量且可能漏短尖峰，不能外推任意題目或更長代數。

House Robber/P16384本輪ASGP為123.798s、GPU為6.505s，倍率19.03×，顯存取樣峰值10.419GiB。這是同設定G8的一對新樣本，不混用先前G4時間。

## 演化品質

成功定義為final training-selected champion在固定train與input-disjoint test都全對；所有結果以各自原生CPU表示離線判題，沒有用test選程式。

| 指標 | ASGP 1T | GAGP GPU-er-o |
| --- | ---: | ---: |
| Final成功runs | 58/150（38.7%） | 39/150（26.0%） |
| Gen0 champion已成功runs | 10/150 | 10/150 |
| Gen0 champion未成功、final成功 | 48/150 | 29/150 |
| 至少一個seed成功的題目 | 16/30 | 9/30 |

相同題目／seed配對中，37對兩邊都成功、21對僅ASGP成功、2對僅GAGP成功、90對都未成功。以上是跨30題的描述性加總，不把它當成150個同分布樣本作顯著性、等價或非劣性推論。

差距較明顯的題目包括Count Odds（ASGP5/5、GPU0/5）、Last Index of Zero（4/5、0/5）、Cumulative Sum（5/5、1/5）；Tribonacci則為3/5、4/5。House Robber為1/5、0/5。每題只有5個seed，完整逐代曲線與每seed結果應一起看；曲線是training-selected champion的test accuracy，允許下降。

初始化已成功的10個runs包括Fibonacci5個、Sum of Elements3個、Subregion Sum及Fibonacci Array各1個。這只描述初代被選中的champion，並未遍歷判斷初代每個個體是否已有其他解，也不能將其計為本輪新增演化收益。

## 可支持的結論與限制

- 本輪支持在這台RTX3090/24GiB上以統一限制跑完整速度表，以及GAGP相對此ASGP1T控制組的粗略搜尋速度改善。每格只有一次cold sample，沒有精密timing信賴區間。
- 品質只在P1024、G64、每題5個配對初代及variation seeds比較，不能判斷不同population的收斂品質，也沒有測相同時間預算的time-to-solution。速度表與品質run的案例數／代數不同，不能直接把速度表倍率套用品質解題時間。
- 初代與資料配對，不代表後續演化軌跡相同。ASGP與GAGP保留不同RNG、operators、ranking與resource units；ASGP phase logical depth7與GAGP physical depth24不同。觀察到的品質落差無法單獨歸因於GPU有損執行。
- ASGP控制是公開benchmark的順序原生Score／operators、elite1、每輪重評完整population；不是stock Run的跨代fitness cache入口。List-output DC使用既有對稱list-combine-v1 extension；不宣稱未修改搜尋語言的原論文重現或純硬體因果加速。
- 品質使用本地ASGP BuildTask資料，並非official PSB fixture。25題採200/1000 unique train/test；五個有限域例外見dataset-splits.json。歷史欄位heldout_outside_training_support只表示test輸入未出現在4096筆原始training pool，不能單憑該數量宣稱分布外推。
- 每次GPU搜尋的first16 final-program CPU fitness核對均通過；品質的每代champion與final另有完整指定train/test判題。這不是新的全population CPU parity證明。

## 來源與重現

Binary凍結於5d4252c，production runtime與12c6849相同。完整性檢查驗證591個檔案雜湊（含外部ASGP46個來源檔案）、五組不同初代、跨seed相同train與disjoint test、600個正式方法runs的命令／seed／案例數／代數、600個judge jobs及程序串行時間區間。

初版分析在held-out判題前凍結。檢查時只修正pool/support文案、明確標示gen0 champion並補充外部source provenance；初版報告保留於logs/day24-20261017-report-initial。修正前後summary、CSV、dataset split及PNG雜湊完全相同，紀錄見analysis-corrections.json；沒有重測或改變成功分類。

[完整逐題報告](REPORT.zh-TW.md) · [互動表](dashboard.html) · [速度CSV](speed.csv) · [品質CSV](quality.csv) · [完整性紀錄](integrity.json) · [來源](provenance.json) · [原始規格與重現](../README.md)
