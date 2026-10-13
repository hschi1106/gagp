## 本輪結論

39次搜尋（30次完整主矩陣＋9次延長），共5184個完整generations，performance runner耗時54.16分鐘；獨立checkpoint audit另10.53秒，出圖另計。P1024/C1024、CPU1T、無profiler，未修改runtime或演化規則。

### 完成範圍

- House：五模式 × 三seeds全部完成64代。
- Median：五模式 × 三seeds全部完成128代。
- Median256：ASGP seeds0/1、GPU-e seed1、GPU-er及GPU-er-o全部三seeds。其他六個延長設定依預先固定的時間預估門檻略過，沒有失敗或被中途終止的run。
- 主fitness圖畫每格最長的完整run；128代虛線標示Median共同範圍。不得用256代個體與其他模式128代終點宣稱同代數品質勝出。

### 比16代多看到了什麼

- **House seed0：ASGP在generation37找到training零誤差解（約32.84秒），held-out也零誤差。** GAGP四模式到64代都尚未找到零誤差；不能以GPU速度代替搜尋品質。
- **House seed1：CPU／GPU-e到64代的training平均誤差3.87，GPU-er/o為4.47，ASGP為5.52。** 三者held-out平均誤差分別4.03、4.63、5.72，這個seed的改善能反映到獨立cases。seed2又是不同排序，不能只挑一個seed下結論。
- **Median seed0：GPU-er/o在128代training正確率53.32%、held-out47.56%；到256代training54.10%、held-out48.73%。** CPU／GPU-e在128代仍停在training37.30%、held-out33.40%；ASGP seed0延長到256代也未突破。
- **Median seed2：CPU／GPU-e在128代達training64.06%、held-out61.13%。** GPU-er/o即使到256代仍為training37.30%、held-out33.40%；ASGP到128代為training43.55%、held-out36.43%。因此不能宣稱GPU reproduction的新variation在所有seed都有相同搜尋能力，也不能把停滯一概歸因題目太難。
- 這些是保留原生語意與不同operator的系統比較，不是證明哪一項規則造成差異的因果實驗。三個seeds共用同一初始population，亦不足以估計收斂成功率。

### 同代數搜尋速度

Search wall含cold與final evaluation，三seed中位數；process另外含匯入／audit／export。以下只比完整主矩陣，不混入延長組。

| 題目／代數 | ASGP 1T | GAGP CPU | GPU-e | GPU-er | GPU-er-o |
| --- | ---: | ---: | ---: | ---: | ---: |
| House／64 | 64.696 s | 457.386 s | 66.812 s | 4.890 s | 4.877 s |
| Median／128 | 5.534 s | 166.595 s | 128.674 s | 3.726 s | 3.751 s |

最快GPU相對ASGP：House **13.27×**、Median **1.49×**。這是會改變population的完整搜尋，不是原frozen single-generation benchmark的30×口徑。

### 後期成本與檢查

- House GPU-er-o的末16代／前16代時間中位數倍率，seeds0/1/2為2.44／1.64／2.14；Median為2.89／1.33／1.61。Median CPU seed2為5.03，品質改善伴隨明顯後期成本。
- 150筆checkpoint、39個去重程式完成完整1024 training及held-out cases重評；native training全部重現，GAGP grammar admission及CPU/GPU checkpoint結果一致。ASGP採其原生runtime重評。
- 所有39runs的前16代best／mean／best_nodes重現原紀錄；9個延長run前128代也重現對應主run，原final128的fitness與延長run generation128一致。
- CPU／GPU-e的best曲線全相同，但House有10個generation的population mean不同，不能宣稱完整population parity；GPU-er/o主矩陣best／mean全相同。
- GPU runs合計36.91億個generation program/case pairs，timeout／fallback／unscored均0；runtime errors約1.13%，仍計入原fitness懲罰。CPU／ASGP缺少同等counter，不以缺值當0。
- 原始frozen inputs與archived binary hashes未變。沒有新增runtime執行緒、演化架構或operator更動。

同時間圖只在實際觀察的範圍比較；House GPU-er/o在64代就結束，不能以停止後的水平延伸線推論它繼續跑到ASGP同時間的品質。`fitness_vs_time_full`展示完整各自時間範圍，`fitness_vs_time`則限制在五模式共同覆蓋的時間。
